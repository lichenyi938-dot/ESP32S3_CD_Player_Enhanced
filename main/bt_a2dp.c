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

/* 版本/兼容宏（同时适配 v4.4 与 v5.x） */
#include "esp_idf_version.h"

/* v4.4: ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND
 * v5.x: ESP_A2D_AUDIO_STATE_SUSPENDED
 */
#if defined(ESP_A2D_AUDIO_STATE_SUSPENDED)
  #define A2DP_STATE_PAUSED  ESP_A2D_AUDIO_STATE_SUSPENDED
#elif defined(ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND)
  #define A2DP_STATE_PAUSED  ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND
#else
  /* 极端 fallback（不应命中），仅为保证编译 */
  #define A2DP_STATE_PAUSED  2
#endif

/* v5.x 的 A2DP 配置在 mcc.sbc.*，v4.4 在 mcc.* */
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  #define MCC_CH_MODE(p_)    ((p_)->audio_cfg.mcc.sbc.ch_mode)    /* 或 channel_mode */
  #define MCC_SAMP_FREQ(p_)  ((p_)->audio_cfg.mcc.sbc.samp_freq)  /* 或 sample_rate  */
#else
  #define MCC_CH_MODE(p_)    ((p_)->audio_cfg.mcc.ch_mode)
  #define MCC_SAMP_FREQ(p_)  ((p_)->audio_cfg.mcc.samp_freq)
#endif

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
        } else if (param->audio_stat.state == A2DP_STATE_PAUSED ||
                   param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
            s_bt_streaming = false;
            s_accum_len = 0;
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT: {
        uint8_t ch_mode   = MCC_CH_MODE(param);
        uint8_t samp_freq = MCC_SAMP_FREQ(param);
        ESP_LOGI(TAG, "A2DP audio cfg ch:%d, sample_rate:%d", ch_mode, samp_freq);
        break;
    }

    default:
        break;
    }
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    /* SBC 解码后 16-bit LE 立体声 PCM */
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

    /* 只用 Classic BT，释放 BLE 内存 */
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mem_release BLE failed: %s", esp_err_to_name(ret));
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(ret));
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

    /* 可被发现/连接 */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

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
