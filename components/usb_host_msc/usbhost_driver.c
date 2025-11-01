/* ====== MSC BOT + 基本 SCSI 实现（内联版，无外部依赖） ====== */
#include <assert.h>

extern esp_err_t usbhost_bulkTransfer(void *data, uint32_t *size,
                                      usbhost_transDir_t dir, uint32_t timeoutMs);

#define MSC_CBW_SIG 0x43425355u   /* 'USBC' */
#define MSC_CSW_SIG 0x53425355u   /* 'USBS' */

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} msc_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} msc_csw_t;

static uint32_t s_cbw_tag = 1;

/* 发送一条 BOT 命令。data_len 可为 0。dir: DEV_TO_HOST / HOST_TO_DEV */
static esp_err_t msc_bot_command(const uint8_t *cdb, uint8_t cdb_len,
                                 void *data, uint32_t data_len,
                                 usbhost_transDir_t dir, uint32_t timeout_ms)
{
    assert(cdb_len <= 16);
    msc_cbw_t cbw = {0};
    cbw.dCBWSignature = MSC_CBW_SIG;
    cbw.dCBWTag       = s_cbw_tag++;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags    = (dir == DEV_TO_HOST) ? 0x80 : 0x00;
    cbw.bCBWLUN       = 0;
    cbw.bCBWCBLength  = cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len);

    /* 1) 发送 CBW（总是 BULK OUT）*/
    uint32_t x = sizeof(msc_cbw_t);
    ESP_RETURN_ON_ERROR(usbhost_bulkTransfer(&cbw, &x, HOST_TO_DEV, timeout_ms),
                        "msc_bot_command: CBW", "");

    /* 2) 数据阶段（如果有） */
    if (data_len > 0) {
        x = data_len;
        esp_err_t er = usbhost_bulkTransfer(data, &x, dir, timeout_ms);
        if (er != ESP_OK) {
            ESP_LOGE("msc_bot", "data stage err=%d", er);
            return er;
        }
    }

    /* 3) 接收 CSW（总是 BULK IN）*/
    msc_csw_t csw = {0};
    x = sizeof(msc_csw_t);
    ESP_RETURN_ON_ERROR(usbhost_bulkTransfer(&csw, &x, DEV_TO_HOST, timeout_ms),
                        "msc_bot_command: CSW", "");

    if (csw.dCSWSignature != MSC_CSW_SIG) {
        ESP_LOGE("msc_bot", "Bad CSW signature: 0x%08x", csw.dCSWSignature);
        return ESP_FAIL;
    }
    if (csw.dCSWTag != cbw.dCBWTag) {
        ESP_LOGE("msc_bot", "Tag mismatch: CBW=%u CSW=%u", cbw.dCBWTag, csw.dCSWTag);
        return ESP_FAIL;
    }
    /* bCSWStatus: 0=Passed, 1=Failed(通常需要 Request Sense), 2=Phase Error */
    if (csw.bCSWStatus == 0) return ESP_OK;
    if (csw.bCSWStatus == 1) return ESP_ERR_INVALID_STATE; /* 需要请求 Sense */
    return ESP_FAIL;
}

/* ---- 基本 SCSI 命令 ---- */
static esp_err_t msc_test_unit_ready(void)
{
    uint8_t cdb[6] = {0x00, 0,0,0,0,0}; /* TEST UNIT READY */
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 5000);
}

static esp_err_t msc_request_sense(uint8_t *sk, uint8_t *asc, uint8_t *ascq)
{
    uint8_t cdb[6] = {0x03, 0, 0, 0, 18, 0}; /* REQUEST SENSE, alloc len 18 */
    uint8_t buf[18] = {0};
    esp_err_t er = msc_bot_command(cdb, 6, buf, sizeof(buf), DEV_TO_HOST, 5000);
    if (sk)   *sk   = buf[2] & 0x0F;
    if (asc)  *asc  = buf[12];
    if (ascq) *ascq = buf[13];
    return er;
}

static esp_err_t msc_prevent_allow_medium_removal(bool prevent)
{
    uint8_t cdb[6] = {0x1E, 0, 0, 0, (uint8_t)(prevent ? 0x01 : 0x00), 0};
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 5000);
}

static esp_err_t msc_start_stop_unit(bool start)
{
    /* START STOP UNIT: bit0 START=1, bit1 LOEJ=1 可弹托盘，这里不弹托盘 */
    uint8_t cdb[6] = {0x1B, 0, 0, 0, (uint8_t)(start ? 0x01 : 0x00), 0};
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 8000);
}

/* ---- 自动上电旋转 & 轮询 ready ---- */
static void usbhost_cd_autostart(void)
{
    const char *TAG = "cd_autostart";

    if (xSemaphoreTake(scsiExeLock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ESP_LOGI(TAG, "Prevent medium removal");
        (void)msc_prevent_allow_medium_removal(true);
        vTaskDelay(pdMS_TO_TICKS(80));

        ESP_LOGI(TAG, "Start spindle");
        (void)msc_start_stop_unit(true);
        xSemaphoreGive(scsiExeLock);
    } else {
        ESP_LOGW(TAG, "scsiExeLock timeout");
    }

    vTaskDelay(pdMS_TO_TICKS(800)); /* 电机起转缓冲 */

    for (int i = 0; i < 30; i++) {  /* ~9s */
        if (xSemaphoreTake(scsiExeLock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        esp_err_t tur = msc_test_unit_ready();
        uint8_t sk=0, asc=0, ascq=0;
        (void)msc_request_sense(&sk,&asc,&ascq);
        xSemaphoreGive(scsiExeLock);

        if (tur == ESP_OK) {
            ESP_LOGI(TAG, "Unit ready");
            break;
        } else {
            ESP_LOGW(TAG, "Not ready yet, sense=%02X/%02X/%02X", sk, asc, ascq);
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
/* ====== 以上新增 ====== */
