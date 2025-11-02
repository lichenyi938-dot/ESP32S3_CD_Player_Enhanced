#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"

#include "usbhost_scsi_cmd.h"
#include "cdPlayer.h"
#include "button.h"
#include "i2s.h"
#include "bt_a2dp.h"

cdplayer_driveInfo_t cdplayer_driveInfo;
cdplayer_playerInfo_t cdplayer_playerInfo;
uint8_t readCdBuf[I2S_TX_BUFFER_LEN];

static const char *TAG = "cdPlayer";

static void printMem(uint8_t *dat, uint16_t size)
{
    for (int i = 0; i < size; i++) printf("%02x ", dat[i]);
    printf("\n");
}

static void log_sense_once(const char *where)
{
    uint8_t s[18] = {0};
    if (usbhost_scsi_requestSense(s) == ESP_OK) {
        uint8_t key = (s[2] & 0x0F);
        ESP_LOGW(TAG, "[%s] SENSE key=%02X ASC=%02X ASCQ=%02X",
                 where, key, s[12], s[13]);
    }
}

/* 等待驱动器就绪：识别常见 SENSE */
static bool cd_wait_ready(uint32_t timeout_ms, uint8_t *trayClosedOut)
{
    TickType_t t0 = xTaskGetTickCount();
    uint8_t s[18] = {0};
    while (pdTICKS_TO_MS(xTaskGetTickCount() - t0) < timeout_ms) {
        if (usbhost_scsi_testUnitReady() == ESP_OK) {
            if (trayClosedOut) *trayClosedOut = 1;
            return true;
        }
        if (usbhost_scsi_requestSense(s) == ESP_OK) {
            uint8_t asc = s[12], ascq = s[13];
            // 介质不存在
            if (asc == 0x3A) {
                if (trayClosedOut) {
                    // 3A/01: Tray open, 3A/02: Tray closed
                    if (ascq == 0x01) *trayClosedOut = 0;
                    else if (ascq == 0x02) *trayClosedOut = 1;
                }
                // 没盘就别再等
                return false;
            }
            // 还在转起/构建TOC：04/01
            if (asc == 0x04 && ascq == 0x01) {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

/* 上电后做一次加载+转起（合上托盘并起转） */
static void cd_spinup_once(void)
{
    // 允许装卸（有的驱动器在禁止装卸状态下不接受 load）
    (void)usbhost_scsi_preventAllowMediumRemoval(false);
    // loej=1,start=1 : load/close + spin up
    (void)usbhost_scsi_startStopUnit(true, true);
}

/* TUR + SENSE 的轻量判断（供监控循环用） */
static esp_err_t cdplayer_discReady(uint8_t *ready, uint8_t *trayClosed)
{
    esp_err_t err = usbhost_scsi_testUnitReady();
    if (err == ESP_OK) {
        *ready = 1;
        *trayClosed = 1;
        return ESP_OK;
    }

    uint8_t s[18] = {0};
    *ready = 0;
    *trayClosed = 1; // 默认先当作关闭

    if (usbhost_scsi_requestSense(s) == ESP_OK) {
        uint8_t asc = s[12], ascq = s[13];
        // 3A/01: 无盘 托盘开；3A/02: 无盘 托盘关
        if (asc == 0x3A) {
            if (ascq == 0x01) *trayClosed = 0;
            else if (ascq == 0x02) *trayClosed = 1;
        }
        // 04/01: 正在变为就绪（比如刚合仓/起转/建TOC）
        if (asc == 0x04 && ascq == 0x01) {
            // 等待方在外层做
        }
    }
    return err;
}

static bool cdplayer_discIsCd(void)
{
    uint8_t senseDat[18];
    esp_err_t err;

    uint8_t requireDat[20];
    uint32_t requireDatLen;

    // Profile List，确认当前 Profile=0x0008 (CD-ROM)
    requireDatLen = 8;
    err = usbhost_scsi_getConfiguration(0x0000, 0x2, requireDat, &requireDatLen);
    if (err != ESP_OK) {
        usbhost_scsi_requestSense(senseDat);
        return false;
    }
    uint16_t currentProfile = __builtin_bswap16(*(uint16_t *)(requireDat + 6));
    if (currentProfile != 0x0008) return false;

    // Disc Information block: Byte8 == 0x00 表示 CD-DA / CD-ROM
    requireDatLen = 9;
    err = usbhost_scsi_readDiscInformation(requireDat, &requireDatLen);
    if (err != ESP_OK) { usbhost_scsi_requestSense(senseDat); return false; }
    if (requireDat[8] != 0x00) return false;

    return true;
}

static esp_err_t cdplayer_getPlayList(uint8_t *tracksCount, cdplayer_trackInfo_t *trackList)
{
    uint8_t senseDat[18];
    esp_err_t err;
    uint8_t *tocDat;

    // 预读 TOC 长度
    uint32_t requireDatLen;
    tocDat = (uint8_t *)malloc(4);
    if (!tocDat) { ESP_LOGE(TAG, "malloc fail"); return ESP_FAIL; }

    requireDatLen = 4;
    err = usbhost_scsi_readTOC(false, 0, tocDat, &requireDatLen);
    if (err != ESP_OK) {
        usbhost_scsi_requestSense(senseDat);
        ESP_LOGE(TAG, "Get toc length fail, key:%02x ASC:%02x%02x",
                 senseDat[2]&0x0F, senseDat[12], senseDat[13]);
        free(tocDat);
        return ESP_FAIL;
    }
    uint32_t tocLen = __builtin_bswap16(((usbhost_scsi_tocHeader_t *)tocDat)->TOC_Data_Length) + 2;
    free(tocDat);

    // 读取完整 TOC
    tocDat = (uint8_t *)malloc(tocLen);
    if (!tocDat) { ESP_LOGE(TAG, "malloc fail"); return ESP_FAIL; }
    requireDatLen = tocLen;
    err = usbhost_scsi_readTOC(false, 0, tocDat, &requireDatLen);
    if (err != ESP_OK) {
        usbhost_scsi_requestSense(senseDat);
        ESP_LOGE(TAG, "Get full toc fail, key:%02x ASC:%02x%02x",
                 senseDat[2]&0x0F, senseDat[12], senseDat[13]);
        free(tocDat);
        return ESP_FAIL;
    }

    // 解析
    usbhost_scsi_tocHeader_t *header = (usbhost_scsi_tocHeader_t *)(tocDat);
    usbhost_scsi_tocTrackDesriptor_t *tocDesc = (usbhost_scsi_tocTrackDesriptor_t *)(tocDat + 4);
    *tracksCount = 0;

    uint16_t tocDataSize = __builtin_bswap16(header->TOC_Data_Length) - 2;
    bool previousTrackIsNotAudio = false;
    for (int i = 0; i < tocDataSize / 8; i++)
    {
        uint32_t trackStartAddress = __builtin_bswap32(tocDesc->Track_Start_Address);
        if (*tracksCount > 0 && !previousTrackIsNotAudio)
            trackList[*tracksCount - 1].trackDuration = trackStartAddress - trackList[*tracksCount - 1].lbaBegin;

        // 非音频轨，跳过
        if ((tocDesc->ADR_CONTROL & 0x0c) != 0x00) {
            previousTrackIsNotAudio = true;
            tocDesc++;
            continue;
        }

        if (tocDesc->Track_Number == 0xaa) break;

        trackList[*tracksCount].trackNum     = tocDesc->Track_Number;
        trackList[*tracksCount].lbaBegin     = trackStartAddress;
        trackList[*tracksCount].preEmphasis  = (tocDesc->ADR_CONTROL & 0x01);
        trackList[*tracksCount].trackDuration= 0;
        trackList[*tracksCount].title        = NULL;
        trackList[*tracksCount].performer    = NULL;
        (*tracksCount)++;
        tocDesc++;
        previousTrackIsNotAudio = false;
    }

    free(tocDat);
    return ESP_OK;
}

static esp_err_t cpplayer_getCdText(char **albumTitle, char **albumPerformer,
                                    char **titleStrBuf, char **performerStrBuf,
                                    uint8_t tracksCount, cdplayer_trackInfo_t *trackList)
{
    uint8_t senseDat[18];
    esp_err_t err;
    uint32_t requireDatLen;
    uint8_t *cdTextDat;

    if (*titleStrBuf) free(*titleStrBuf);
    if (*performerStrBuf) free(*performerStrBuf);

    // 检查是否支持 CD-Text
    uint8_t requireDat[20];
    requireDatLen = 16;
    err = usbhost_scsi_getConfiguration(0x001e, 0x2, requireDat, &requireDatLen);
    if (err != ESP_OK) { usbhost_scsi_requestSense(senseDat); return err; }
    if (requireDatLen <= 8) return ESP_FAIL;
    if ((requireDat[12] & 0x01) != 1) return ESP_FAIL;

    // 预读 CD-Text 长度
    cdTextDat = (uint8_t *)malloc(4);
    if (!cdTextDat) { ESP_LOGE(TAG, "malloc fail"); return ESP_FAIL; }
    requireDatLen = 4;
    err = usbhost_scsi_readTOC(false, 5, cdTextDat, &requireDatLen);
    if (err != ESP_OK) {
        usbhost_scsi_requestSense(senseDat);
        ESP_LOGE(TAG, "Get CD-TEXT length fail, key:%02x ASC:%02x%02x",
                 senseDat[2]&0x0F, senseDat[12], senseDat[13]);
        free(cdTextDat);
        return ESP_FAIL;
    }
    uint32_t tocLen = __builtin_bswap16(((usbhost_scsi_tocHeader_t *)cdTextDat)->TOC_Data_Length) + 2;
    free(cdTextDat);

    // 读完整 CD-Text
    cdTextDat = (uint8_t *)malloc(tocLen);
    if (!cdTextDat) { ESP_LOGE(TAG, "malloc fail"); return ESP_FAIL; }
    requireDatLen = tocLen;
    err = usbhost_scsi_readTOC(false, 5, cdTextDat, &requireDatLen);
    if (err != ESP_OK) {
        usbhost_scsi_requestSense(senseDat);
        ESP_LOGE(TAG, "Get full CD-TEXT fail, key:%02x ASC:%02x%02x",
                 senseDat[2]&0x0F, senseDat[12], senseDat[13]);
        free(cdTextDat);
        return ESP_FAIL;
    }

    // 解析字符串
    usbhost_scsi_tocHeader_t *header = (usbhost_scsi_tocHeader_t *)(cdTextDat);
    usbhost_scsi_tocCdTextDesriptor_t *textSequence = (usbhost_scsi_tocCdTextDesriptor_t *)(cdTextDat + 4);

    uint16_t cdTextDescSize = __builtin_bswap16(header->TOC_Data_Length) - 2;
    if (cdTextDescSize % 18 != 0) {
        ESP_LOGE(TAG, "CD-TEXT descriptor length invalid. len=%d", cdTextDescSize);
        free(cdTextDat);
        return ESP_FAIL;
    }

    uint16_t titleStrBufSize = 0, performerStrBufSize = 0;
    for (int i = 0; i < cdTextDescSize / 18; i++) {
        if ((textSequence + i)->ID1 == 0x80) titleStrBufSize     += 12;
        if ((textSequence + i)->ID1 == 0x81) performerStrBufSize += 12;
    }

    *titleStrBuf = (char *)malloc(titleStrBufSize ? titleStrBufSize : 1);
    *performerStrBuf = (char *)malloc(performerStrBufSize ? performerStrBufSize : 1);
    if (!*titleStrBuf || !*performerStrBuf) {
        ESP_LOGE(TAG, "String buffer malloc fail");
        if (*titleStrBuf) free(*titleStrBuf);
        if (*performerStrBuf) free(*performerStrBuf);
        free(cdTextDat);
        return ESP_FAIL;
    }
    memset(*titleStrBuf, 0, titleStrBufSize);
    memset(*performerStrBuf, 0, performerStrBufSize);

    int titleStrFoundCount = 0, titleStrInsertAt = 0;
    int performerStrFoundCount = 0, performerStrInsertAt = 0;
    for (int i = 0; i < cdTextDescSize / 18; i++) {
        if (textSequence->ID1 == 0x80) { // title
            uint8_t trackNum = textSequence->ID2;
            if (trackNum != titleStrFoundCount) {
                ESP_LOGE(TAG, "Title sequence track mismatch");
                printMem(cdTextDat, cdTextDescSize);
                free(*titleStrBuf); free(*performerStrBuf); free(cdTextDat);
                return ESP_FAIL;
            }
            if ((textSequence->ID4 & 0x0F) == 0) {
                if (trackNum == 0) *albumTitle = *titleStrBuf + titleStrInsertAt;
                else trackList[trackNum - 1].title = *titleStrBuf + titleStrInsertAt;
            }
            memcpy(*titleStrBuf + titleStrInsertAt, textSequence->text, 12);
            for (int c = 0; c < 12; c++) {
                if (textSequence->text[c] == '\0' && trackNum <= tracksCount) {
                    trackList[trackNum].title = *titleStrBuf + titleStrInsertAt + c + 1;
                    trackNum++;
                }
            }
            titleStrFoundCount = trackNum;
            titleStrInsertAt += 12;
        } else if (textSequence->ID1 == 0x81) { // performer
            uint8_t trackNum = textSequence->ID2;
            if (trackNum != performerStrFoundCount) {
                ESP_LOGE(TAG, "Performer sequence track mismatch");
                printMem(cdTextDat, cdTextDescSize);
                free(*titleStrBuf); free(*performerStrBuf); free(cdTextDat);
                return ESP_FAIL;
            }
            if ((textSequence->ID4 & 0x0F) == 0) {
                if (trackNum == 0) *albumPerformer = *performerStrBuf + performerStrInsertAt;
                else trackList[trackNum - 1].performer = *performerStrBuf + performerStrInsertAt;
            }
            memcpy(*performerStrBuf + performerStrInsertAt, textSequence->text, 12);
            for (int c = 0; c < 12; c++) {
                if (textSequence->text[c] == '\0' && trackNum <= tracksCount) {
                    trackList[trackNum].performer = *performerStrBuf + performerStrInsertAt + c + 1;
                    trackNum++;
                }
            }
            performerStrFoundCount = trackNum;
            performerStrInsertAt += 12;
        }
        textSequence++;
    }

    free(cdTextDat);
    return ESP_OK;
}

static void volumeStep(int upDown)
{
    cdplayer_playerInfo.volume += upDown;
    if (cdplayer_playerInfo.volume > 30) cdplayer_playerInfo.volume = 30;
    if (cdplayer_playerInfo.volume < 0)  cdplayer_playerInfo.volume = 0;
    ESP_LOGI("volumeStep", "Volume: %d", cdplayer_playerInfo.volume);
}

static void cdplayer_task_deviceAndDiscMonitor(void *arg)
{
    esp_err_t err;
    uint8_t responDat[32] = {0};
    uint32_t responSize;

    while (1) {
        cdplayer_driveInfo.discInserted   = 0;
        cdplayer_driveInfo.discIsCD       = 0;
        cdplayer_driveInfo.trayClosed     = 1;
        cdplayer_driveInfo.trackCount     = 0;
        cdplayer_driveInfo.cdTextAvalibale= 0;
        cdplayer_driveInfo.readyToPlay    = 0;
        cdplayer_driveInfo.albumTitle     = NULL;
        cdplayer_driveInfo.albumPerformer = NULL;
        cdplayer_driveInfo.strBuf_titles  = NULL;
        cdplayer_driveInfo.strBuf_performers = NULL;

        cdplayer_playerInfo.playing            = 0;
        cdplayer_playerInfo.playingTrackIndex  = 0;
        cdplayer_playerInfo.readFrameCount     = 0;

        strcpy(cdplayer_driveInfo.vendor, "");
        strcpy(cdplayer_driveInfo.product, "");

        // 等设备连接
        while (usbhost_driverObj.deviceIsOpened == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            printf("Wait for usb cd drive connect.\n");
        }

        // 检查是不是 CD/DVD
        ESP_LOGI(TAG, "Check if usb device is CD/DVD device.");
        responSize = sizeof(responDat);
        err = usbhost_scsi_inquiry(responDat, &responSize);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SCSI inquiry cmd fail: %d", err);
            continue;
        }
        if ((responDat[0] & 0x0F) != 0x05) {
            printf("Not CD/DVD device.\n");
            usbhost_closeDevice();
            continue;
        }
        if (responSize >= 32) {
            memcpy(cdplayer_driveInfo.vendor,  (responDat + 8), 8);
            cdplayer_driveInfo.vendor[8] = '\0';
            memcpy(cdplayer_driveInfo.product, (responDat + 16), 16);
            cdplayer_driveInfo.product[16] = '\0';
            int i;
            for (i = 7;  i >= 0 && cdplayer_driveInfo.vendor[i]  == ' '; i--) cdplayer_driveInfo.vendor[i]  = '\0';
            for (i = 15; i >= 0 && cdplayer_driveInfo.product[i] == ' '; i--) cdplayer_driveInfo.product[i] = '\0';
            printf("Device model: %s %s\n", cdplayer_driveInfo.vendor, cdplayer_driveInfo.product);
        }

        /* 关键：上电后先做一次合仓+起转，并等待就绪 */
        cd_spinup_once();
        (void)cd_wait_ready(20000, &cdplayer_driveInfo.trayClosed);

        // 等待放入光盘
        ESP_LOGI(TAG, "Wait for disc insert");
        while (1) {
            err = cdplayer_discReady(&cdplayer_driveInfo.discInserted,
                                     &cdplayer_driveInfo.trayClosed);
            if (err == ESP_OK || !usbhost_driverObj.deviceIsOpened) break;

            printf("Unit not ready, tray: %s\n",
                   cdplayer_driveInfo.trayClosed ? "Closed" : "Open");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (!usbhost_driverObj.deviceIsOpened) continue;

        // 确认是音频 CD
        ESP_LOGI(TAG, "Check if disc is cdda");
        cdplayer_driveInfo.discIsCD = cdplayer_discIsCd();
        if (!cdplayer_driveInfo.discIsCD) {
            ESP_LOGE(TAG, "Not CD-DA");
            goto WAIT_FOR_DISC_REMOVE;
        }

        // 读 TOC
        ESP_LOGI(TAG, "Read TOC");
        err = cdplayer_getPlayList(&cdplayer_driveInfo.trackCount, cdplayer_driveInfo.trackList);
        if (err != ESP_OK) continue;
        if (cdplayer_driveInfo.trackCount == 0) {
            ESP_LOGI(TAG, "Not CD-DA");
            cdplayer_driveInfo.discIsCD = 0;
            goto WAIT_FOR_DISC_REMOVE;
        }

        // 读 CD-TEXT（可选）
        ESP_LOGI(TAG, "Read CD-TEXT");
        cdplayer_driveInfo.cdTextAvalibale = false;
        err = cpplayer_getCdText(
            &cdplayer_driveInfo.albumTitle, &cdplayer_driveInfo.albumPerformer,
            &cdplayer_driveInfo.strBuf_titles, &cdplayer_driveInfo.strBuf_performers,
            cdplayer_driveInfo.trackCount, cdplayer_driveInfo.trackList);
        if (err == ESP_OK) {
            cdplayer_driveInfo.cdTextAvalibale = true;
            printf("Album performer: %s\nAlbum title: %s\n",
                   cdplayer_driveInfo.albumPerformer, cdplayer_driveInfo.albumTitle);
        } else {
            ESP_LOGI(TAG, "CD-TEXT not found");
            if (cdplayer_driveInfo.strBuf_titles)     free(cdplayer_driveInfo.strBuf_titles);
            if (cdplayer_driveInfo.strBuf_performers) free(cdplayer_driveInfo.strBuf_performers);
        }

        // 打印播放列表
        printf("**********PlayList**********\n");
        for (int i = 0; i < cdplayer_driveInfo.trackCount; i++) {
            printf("Track: %02d, begin: %6ld, duration: %6ld, preEmphasis: %d ",
                   cdplayer_driveInfo.trackList[i].trackNum,
                   cdplayer_driveInfo.trackList[i].lbaBegin,
                   cdplayer_driveInfo.trackList[i].trackDuration,
                   cdplayer_driveInfo.trackList[i].preEmphasis);
            if (cdplayer_driveInfo.cdTextAvalibale)
                printf("%s - %s\n",
                       cdplayer_driveInfo.trackList[i].performer,
                       cdplayer_driveInfo.trackList[i].title);
            else
                printf("\n");
        }

        cdplayer_driveInfo.readyToPlay = 1;
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 等待碟片弹出或光驱移除
WAIT_FOR_DISC_REMOVE:
        while (1) {
            for (int i = 0; i < 7; i++) {
                err = cdplayer_discReady(&cdplayer_driveInfo.discInserted,
                                         &cdplayer_driveInfo.trayClosed);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (err != ESP_OK) {
                ESP_LOGI(TAG, "Disc removed");
                break;
            }
            if (usbhost_driverObj.deviceIsOpened != 1) {
                ESP_LOGI(TAG, "CD drive disconnected");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (cdplayer_driveInfo.cdTextAvalibale) {
            free(cdplayer_driveInfo.strBuf_titles);
            free(cdplayer_driveInfo.strBuf_performers);
        }
    }
}

static void cdplayer_task_playControl(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        btn_renew(0);

        // 弹出碟片：loej=1,start=0
        if (btn_getPosedge(BTN_EJECT)) {
            if (usbhost_driverObj.deviceIsOpened == 1) {
                ESP_LOGI("cdplayer_task_playControl", "Eject disc");
                cdplayer_playerInfo.playing = 0;
                esp_err_t err = usbhost_scsi_startStopUnit(true, false);
                if (err != ESP_OK) log_sense_once("Eject");
            }
        }

        // 音量调整
        static bool volumHasChange = false;
        if (btn_getNegedge(BTN_VOL_UP))   { volumHasChange = true; volumeStep(1); }
        if (btn_getNegedge(BTN_VOL_DOWN)) { volumHasChange = true; volumeStep(-1); }

        static struct timeval t;
        struct timeval now;
        if (btn_getLongPress(BTN_VOL_UP, 0)) {
            gettimeofday(&now, NULL);
            uint32_t ms = (now.tv_sec - t.tv_sec) * 1000 + (now.tv_usec - t.tv_usec) / 1000;
            if (ms > 50) { gettimeofday(&t, NULL); volumeStep(1); volumHasChange = true; }
        }
        if (btn_getLongPress(BTN_VOL_DOWN, 0)) {
            gettimeofday(&now, NULL);
            uint32_t ms = (now.tv_sec - t.tv_sec) * 1000 + (now.tv_usec - t.tv_usec) / 1000;
            if (ms > 50) { gettimeofday(&t, NULL); volumeStep(-1); volumHasChange = true; }
        }

        // 保存音量（非播放时）
        if (volumHasChange && !cdplayer_playerInfo.playing) {
            volumHasChange = false;
            int8_t savedValue;
            nvs_handle_t h;
            nvs_open("storage", NVS_READWRITE, &h);
            nvs_get_i8(h, "vol", &savedValue);
            if (savedValue != cdplayer_playerInfo.volume) {
                nvs_set_i8(h, "vol", cdplayer_playerInfo.volume);
                nvs_commit(h);
            }
            nvs_close(h);
            ESP_LOGI("cdplayer_task_playControl", "volume saved.");
        }

        // 快进快退
        if (btn_getLongPress(BTN_NEXT, 0)) {
            if (cdplayer_driveInfo.readyToPlay == 1) {
                cdplayer_playerInfo.fastForwarding = 1;
                cdplayer_playerInfo.readFrameCount += 5;
                if (cdplayer_playerInfo.readFrameCount >
                    cdplayer_driveInfo.trackList[cdplayer_playerInfo.playingTrackIndex].trackDuration)
                    cdplayer_playerInfo.readFrameCount =
                        cdplayer_driveInfo.trackList[cdplayer_playerInfo.playingTrackIndex].trackDuration;
            }
        } else if (btn_getLongPress(BTN_PREVIOUS, 0)) {
            if (cdplayer_driveInfo.readyToPlay == 1) {
                cdplayer_playerInfo.fastBackwarding = 1;
                if (cdplayer_playerInfo.readFrameCount >= 5) cdplayer_playerInfo.readFrameCount -= 5;
                else cdplayer_playerInfo.readFrameCount = 0;
            }
        }

        // 上/下一曲
        if (btn_getPosedge(BTN_NEXT)) {
            if (cdplayer_playerInfo.fastForwarding) cdplayer_playerInfo.fastForwarding = 0;
            else if (cdplayer_driveInfo.readyToPlay == 1) {
                cdplayer_playerInfo.readFrameCount = 0;
                cdplayer_playerInfo.playingTrackIndex++;
                if (cdplayer_playerInfo.playingTrackIndex >= cdplayer_driveInfo.trackCount)
                    cdplayer_playerInfo.playingTrackIndex = 0;
                ESP_LOGI("cdplayer_task_playControl", "Next, track: %d", cdplayer_playerInfo.playingTrackIndex);
            }
        } else if (btn_getPosedge(BTN_PREVIOUS)) {
            if (cdplayer_playerInfo.fastBackwarding) cdplayer_playerInfo.fastBackwarding = 0;
            else if (cdplayer_driveInfo.readyToPlay == 1) {
                cdplayer_playerInfo.readFrameCount = 0;
                cdplayer_playerInfo.playingTrackIndex--;
                if (cdplayer_playerInfo.playingTrackIndex < 0)
                    cdplayer_playerInfo.playingTrackIndex = cdplayer_driveInfo.trackCount - 1;
                ESP_LOGI("cdplayer_task_playControl", "Previous, play: %d", cdplayer_playerInfo.playingTrackIndex);
            }
        }

        // 播放/暂停
        if (btn_getPosedge(BTN_PLAY)) {
            if (cdplayer_driveInfo.readyToPlay == 1) {
                cdplayer_playerInfo.playing = !cdplayer_playerInfo.playing;
                ESP_LOGI("cdplayer_task_playControl", "Play: %d", cdplayer_playerInfo.playing);
                if (cdplayer_playerInfo.playing) {
                    esp_err_t err = usbhost_scsi_setCDSpeed(65535);
                    if (err != ESP_OK) log_sense_once("Set speed");
                }
            }
        }

        // 读盘送 I2S
        if (cdplayer_driveInfo.readyToPlay == 1 && cdplayer_playerInfo.playing &&
            !cdplayer_playerInfo.fastForwarding && !cdplayer_playerInfo.fastBackwarding &&
            !i2s_bufsFull)
        {
            int8_t *trackNo = &cdplayer_playerInfo.playingTrackIndex;
            uint32_t trackDuration = cdplayer_driveInfo.trackList[*trackNo].trackDuration;
            int32_t *readFrameCount = &cdplayer_playerInfo.readFrameCount;
            uint32_t remainFrame = trackDuration - *readFrameCount;

            uint32_t readFrames = (remainFrame > I2S_TX_BUFFER_SIZE_FRAME) ? I2S_TX_BUFFER_SIZE_FRAME : remainFrame;
            uint32_t readBytes  = readFrames * 2352;
            uint32_t readLba    = cdplayer_driveInfo.trackList[*trackNo].lbaBegin + *readFrameCount;

            esp_err_t err = usbhost_scsi_readCD(readLba, readCdBuf, &readFrames, &readBytes);
            if (err == ESP_OK) {
                if (!bt_is_active()) { i2s_fillBuffer(readCdBuf); }
                *readFrameCount += readFrames;
            } else {
                printf("Read fail, lba: %ld len(bytes): %ld\n", readLba, readBytes);
                log_sense_once("ReadCD");
            }

            // 当前曲目结束
            if (*readFrameCount >= trackDuration) {
                *readFrameCount = 0;
                (*trackNo)++;
                if (*trackNo >= cdplayer_driveInfo.trackCount) {
                    cdplayer_playerInfo.playing = 0;
                    *trackNo = 0;
                    ESP_LOGI("cdplayer_task_playControl", "Finish");
                } else {
                    ESP_LOGI("cdplayer_task_playControl", "Play next track: %02d", (*trackNo) + 1);
                }
            }
        }
    }
}

void cdplay_init(void)
{
    // 读音量
    nvs_handle_t my_handle;
    esp_err_t err;
    nvs_open("storage", NVS_READWRITE, &my_handle);
    err = nvs_get_i8(my_handle, "vol", &cdplayer_playerInfo.volume);
    if (err != ESP_OK) cdplayer_playerInfo.volume = 10;
    nvs_close(my_handle);

    BaseType_t ret;
    ret = xTaskCreatePinnedToCore(cdplayer_task_deviceAndDiscMonitor,
                                  "cdplayer_task_deviceAndDiscMonitor",
                                  4096, NULL, 2, NULL, 0);
    if (ret != pdPASS) ESP_LOGE("cdplay_init", "deviceAndDiscMonitor create fail");

    ret = xTaskCreatePinnedToCore(cdplayer_task_playControl,
                                  "cdplayer_task_playControl",
                                  4096, NULL, 3, NULL, 0);
    if (ret != pdPASS) ESP_LOGE("cdplay_init", "playControl create fail");
}

hmsf_t cdplay_frameToHmsf(uint32_t frame)
{
    int sec = frame / 75;
    hmsf_t result = {
        .hour   = sec / 3600,
        .minute = (sec % 3600) / 60,
        .second = sec % 60,
        .frame  = frame % 75,
    };
    return result;
}
