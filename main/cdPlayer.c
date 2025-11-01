#include "cdPlayer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cdplay_stub";

/* ====== 桩状态 ====== */
static bool s_ready = false;
static bool s_playing = false;
static int  s_volume = 50;

bool cdplay_init(void)
{
    ESP_LOGI(TAG, "init (stub)");
    s_ready = true;      // 标记为就绪，方便上层逻辑继续跑
    s_playing = false;
    return true;
}

void cdplay_deinit(void)
{
    ESP_LOGI(TAG, "deinit (stub)");
    s_ready = false;
    s_playing = false;
}

bool cdplay_devInit(void)
{
    ESP_LOGI(TAG, "device init (stub) -> ready");
    s_ready = true;
    return s_ready;
}

bool cdplay_isReady(void)
{
    return s_ready;
}

bool cdplay_isPlaying(void)
{
    return s_playing;
}

void cdplay_play(void)
{
    if (!s_ready) return;
    s_playing = true;
    ESP_LOGI(TAG, "play (stub)");
}

void cdplay_stop(void)
{
    s_playing = false;
    ESP_LOGI(TAG, "stop (stub)");
}

void cdplay_eject(void)
{
    s_playing = false;
    ESP_LOGI(TAG, "eject (stub)");
}

void cdplay_next(void)
{
    ESP_LOGI(TAG, "next (stub)");
}

void cdplay_prev(void)
{
    ESP_LOGI(TAG, "prev (stub)");
}

void cdplay_setVolume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    s_volume = vol;
    ESP_LOGI(TAG, "set volume = %d (stub)", s_volume);
}

int cdplay_getVolume(void)
{
    return s_volume;
}

/* 红皮书：1 秒 = 75 帧；这里只按 frames 直接换算 */
hmsf_t cdplay_frameToHmsf(uint32_t frames)
{
    hmsf_t t = {0, 0, 0, 0};
    uint32_t total_seconds = frames / 75;
    t.frame  = frames % 75;

    t.hour   = total_seconds / 3600;
    total_seconds %= 3600;
    t.minute = total_seconds / 60;
    t.second = total_seconds % 60;

    return t;
}
