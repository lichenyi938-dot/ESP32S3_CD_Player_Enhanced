#include "esp_log.h"
#include <stdbool.h>
#include "bt_a2dp.h"

static const char *TAG = "bt_a2dp";

bool bt_is_active(void) { return false; }

void bt_a2dp_init(void)
{
    ESP_LOGW(TAG, "A2DP is NOT supported on ESP32-S3 (no Bluetooth Classic). Skipping init.");
}

void bt_a2dp_shutdown(void) { /* nothing */ }
