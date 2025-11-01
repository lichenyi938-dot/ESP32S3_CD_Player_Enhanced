#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// 如本文件后续需要队列/信号量，再打开下面两行
// #include "freertos/queue.h"
// #include "freertos/semphr.h"

#include "esp_log.h"
#include "main.h"
#include "iic.h"
#include "oled.h"
#include "cdPlayer.h"
#include "usbhost_driver.h"

void task_oled(void *args)
{
    OLED_Init();
    vTaskDelay(pdMS_TO_TICKS(123));

    char str[64];

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(50));

        /* 驱动器型号 */
        (void)snprintf(str, sizeof(str), "%s-%s",
                       cdplayer_driveInfo.vendor,
                       cdplayer_driveInfo.product);
        OLED_ShowString(0, 0, str, 0);

        /* 碟状态 */
        if (usbhost_driverObj.deviceIsOpened == 0)
            OLED_ShowString(0, 1, "No drive", 0);
        else if (cdplayer_driveInfo.trayClosed == 0)
            OLED_ShowString(0, 1, "Tray open", 0);
        else if (cdplayer_driveInfo.discInserted == 0)
            OLED_ShowString(0, 1, "No disc", 0);
        else if (cdplayer_driveInfo.discIsCD == 0)
            OLED_ShowString(0, 1, "Not cdda", 0);
        else
            OLED_ShowString(0, 1, " Ready ", 1);

        /* 音量 */
        (void)snprintf(str, sizeof(str), "VOL: %02d", (int)cdplayer_playerInfo.volume);
        OLED_ShowString(0, 5, str, 0);

        if (cdplayer_driveInfo.readyToPlay)
        {
            /* 播放状态 */
            if (cdplayer_playerInfo.fastForwarding)
                OLED_ShowString(95, 1, ">>>", 0);
            else if (cdplayer_playerInfo.fastBackwarding)
                OLED_ShowString(95, 1, "<<<", 0);
            else if (cdplayer_playerInfo.playing)
                OLED_ShowString(95, 1, "Play", 0);
            else
                OLED_ShowString(95, 1, "Pause", 0);

            /* 轨号保护，防止越界 */
            int8_t trackNum = cdplayer_playerInfo.playingTrackIndex;
            if (trackNum < 0 || (uint8_t)trackNum >= cdplayer_driveInfo.trackCount) {
                trackNum = 0;
            }

            /* 碟名 */
            if (cdplayer_driveInfo.cdTextAvalibale && cdplayer_driveInfo.albumTitle)
                OLED_ShowString(0, 2, cdplayer_driveInfo.albumTitle, 0);

            /* 轨名/轨号 */
            if (cdplayer_driveInfo.cdTextAvalibale &&
                cdplayer_driveInfo.trackList[trackNum].title)
            {
                (void)snprintf(str, sizeof(str), "%-15.15s %02d/%02d",
                               cdplayer_driveInfo.trackList[trackNum].title,
                               (int)trackNum + 1,
                               (int)cdplayer_driveInfo.trackCount);
            }
            else
            {
                (void)snprintf(str, sizeof(str), "Track %02d       %02d/%02d",
                               (int)cdplayer_driveInfo.trackList[trackNum].trackNum,
                               (int)trackNum + 1,
                               (int)cdplayer_driveInfo.trackCount);
            }
            OLED_ShowString(0, 3, str, 0);

            /* 演唱者 */
            if (cdplayer_driveInfo.cdTextAvalibale &&
                cdplayer_driveInfo.trackList[trackNum].performer)
            {
                OLED_ShowString(0, 4, cdplayer_driveInfo.trackList[trackNum].performer, 0);
            }

            /* 预加重 */
            if (cdplayer_driveInfo.trackList[trackNum].preEmphasis)
                OLED_ShowString(100, 5, " PEM ", 1);

            /* 播放时长 & 进度条 */
            uint32_t readFrameCount = cdplayer_playerInfo.readFrameCount;
            uint32_t trackDuration  = cdplayer_driveInfo.trackList[trackNum].trackDuration;

            if (cdplayer_driveInfo.readyToPlay)
            {
                hmsf_t now      = cdplay_frameToHmsf(readFrameCount);
                hmsf_t duration = cdplay_frameToHmsf(trackDuration);
                (void)snprintf(str, sizeof(str), "%02d:%02d.%02d     %02d:%02d.%02d",
                               (int)now.minute, (int)now.second, (int)now.frame,
                               (int)duration.minute, (int)duration.second, (int)duration.frame);
                OLED_ShowString(0, 6, str, 0);
            }

            float progress = 0.0f;
            if (trackDuration > 0) {
                progress = ((float)readFrameCount) / ((float)trackDuration);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;
            }
            OLED_progressBar(7, progress);
        }
        else
        {
            OLED_ShowString(0, 3, "X_X", 0);
            (void)snprintf(str, sizeof(str), "%02d:%02d.%02d     %02d:%02d.%02d",
                           0, 0, 0, 0, 0, 0);
            OLED_ShowString(0, 6, str, 0);
            OLED_progressBar(7, 0.0f);
        }

        OLED_refreshScreen();
    }
}
