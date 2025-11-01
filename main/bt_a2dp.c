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

#include "i2s.h"
#include "bt_a2dp.h"

static const char *TAG = "bt_a2dp";

static volatile bool s_bt_streaming = false;
static uint8_t s_accum_buf[I2S_TX_BUFFER_LEN];
static size_t s_accum_len = 0;

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
    case ESP_A2D_AUDIO_CFG_EVT:
        // report audio cfg, to set i2s sample rate if needed
        ESP_LOGI(TAG, "A2DP audio cfg ch:%d, sample_rate:%d", 
                 param->audio_cfg.mcc.ch_mode, 
                 param->audio_cfg.mcc.samp_freq);
        break;
    default:
        break;
    }
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    // data is PCM 16-bit stereo little-endian (from SBC decoder)
    // Accumulate to I2S buffer sized I2S_TX_BUFFER_LEN then push
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
    // release BLE memory (not used)
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
        ESP_LOGE(TAG, "A2DP sink init failed: %s", esp_err_to_name(ret));
        return;
    }

    // set discoverable/connectable
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // init AVRCP for remote control (optional)

    ESP_LOGI(TAG, "A2DP Sink ready, scan and connect from phone");
}

void bt_a2dp_shutdown(void)
{
    s_bt_streaming = false;
    esp_a2d_sink_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
}