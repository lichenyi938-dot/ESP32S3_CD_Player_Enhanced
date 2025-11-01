#include <string.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* 关键：用于判断当前 ESP-IDF 版本，以兼容 v4.4 / v5.x */
#include "esp_idf_version.h"

#include "i2s.h"
#include "bt_a2dp.h"

static const char *TAG = "bt_a2dp";

static volatile bool s_bt_streaming = false;
static uint8_t s_accum_buf[I2S_TX_BUFFER_LEN];
static size_t  s_accum_len = 0;

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "authentication complete, status:%d", param->auth_cmpl.stat);
        break;
    default:
        break;
    }
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "A2DP connection state: %d", param->conn_stat.state);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "A2DP audio state: %d", param->audio_stat.state);
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            s_bt_streaming = true;
            s_accum_len = 0;
        } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPENDED ||
                   param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
            s_bt_streaming = false;
            s_accum_len = 0;
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT: {
        /* v4.4 与 v5.x 的结构体层级不同：v5.x 在 mcc.sbc 下 */
        uint8_t ch_mode   = 0;
        uint8_t samp_freq = 0;

    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        ch_mode   = param->audio_cfg.mcc.sbc.ch_mode;    /* 或 channel_mode（不同小版本命名略有差异） */
        samp_freq = param->audio_cfg.mcc.sbc.samp_freq;  /* 或 sample_rate  */
    #else
        ch_mode   = param->audio_cfg.mcc.ch_mode;
        samp_freq = param->audio_cfg.mcc.samp_freq;
    #endif
        ESP_LOGI(TAG, "A2DP audio cfg ch:%d, sample_rate:%d", ch_mode, samp_freq);
        break;
    }

    default:
        break;
    }
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    /* data: SBC 解码后的 16bit 小端 双声道 PCM */
    while (len > 0) {
        uint32_t tocopy = I2S_TX_BUFFER_LEN - s_accum_len;
        if (tocopy > len) tocopy = len;

        memcpy(s_accum_buf + s_accum_len, data, tocopy);
        s_accum_len += tocopy;
        data += tocopy;
        len  -= tocopy;

        if (s_accum_len >= I2S_TX_BUFFER_LEN) {
            i2s_fillBuffer(s_accum_buf);
            s_accum_len = 0;
        }
    }
}

bool bt_is_active(void)
{
    return s_bt_streaming;
}

void bt_a2dp_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Init BT controller");

    /* 释放 BLE 内存（只用 Classic BT） */
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_bt_controller_mem_release BLE failed: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "%s bluedroid init failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "%s bluedroid enable failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    esp_bt_dev_set_device_name("ESP32_CD_Player");

    esp_bt_gap_register_callback(bt_app_gap_cb);

    esp_a2d_register_callback(bt_app_a2d_cb);
    esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

    ret = esp_a2d_sink_init();
    if (ret != ESP_OK) {
