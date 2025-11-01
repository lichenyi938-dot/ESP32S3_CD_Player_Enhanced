#include "cdPlayer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cdplay_stub";

/* ========== 提供 GUI 直接读取的全局信息（最小可用） ========== */
cdplayer_drive_info_t  cdplayer_driveInfo  = {
    .vendor   = "UNKNOWN",
    .product  = "CD/DVD",
    .revision = "1.0"
};

cdplayer_player_info_t cdplayer_playerInfo = {
    .volume  = 50,
    .ready   = false,
    .playing = false
};

/* ========== 内部桩状态 ========== */
static bool s_ready   = false;
static bool s_playing = false;

/* ========== 初始化/反初始化 ========== */
bool cdplay_init(void)
{
    ESP_LOGI(TAG, "init (stub)");
    s_ready = true;
    s_playing = false;

    /* 给 GUI 一些可显示的默认文案 */
    strncpy(cdplayer_driveInfo.vendor,  "ESP", sizeof(cdplayer_driveInfo.vendor)-1);
    strncpy(cdplayer_driveInfo.product, "USB-ODD", sizeof(cdplayer_driveInfo.product)-1);
    strncpy(cdplayer_driveInfo.revision,"1.0", sizeof(cdplayer_driveInfo.revision)-1);

    cdplayer_playerInfo.ready   = s_ready;
    cdplayer_playerInfo.playing = s_playing;
    return true;
}

void cdplay_deinit(void)
{
    ESP_LOGI(TAG, "deinit (stub)");
    s_ready = false;
    s_playing = false;
    cdplayer_playerInfo.ready   = s_ready;
    cdplayer_playerInfo.playing = s_playing;
}

/* 设备初始化（真实项目里可做 SCSI/ATAPI/USB 初始化） */
bool cdplay_devInit(void)
{
    ESP_LOGI(TAG, "device init (stub) -> ready");
    s_ready = true;
    cdplayer_playerInfo.ready = true;
    return true;
}

/* ========== 状态查询 ========== */
bool cdplay_isReady(void)   { return s_ready;   }
bool cdplay_isPlaying(void) { return s_playing; }

/* ========== 播放控制（桩） ========== */
void cdplay_play(void)
{
    if (!s_ready) return;
    s_playing = true;
    cdplayer_playerInfo.playing = true;
    ESP_LOGI(TAG, "play (stub)");
}

void cdplay_stop(void)
{
    s_playing = false;
    cdplayer_playerInfo.playing = false;
    ESP_LOGI(TAG, "stop (stub)");
}

void cdplay_eject(void) { ESP_LOGI(TAG, "eject (stub)"); }
void cdplay_next(void)  { ESP_LOGI(TAG, "next (stub)");  }
void cdplay_prev(void)  { ESP_LOGI(TAG, "prev (stub)");  }

/* ========== 音量 ========== */
void cdplay_setVolume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    cdplayer_playerInfo.volume = vol;
    ESP_LOGI(TAG, "set volume = %d (stub)", cdplayer_playerInfo.volume);
}

int cdplay_getVolume(void)
{
    return cdplayer_playerInfo.volume;
}

/* ========== 工具函数：帧 -> H:M:S:F（75fps） ========== */
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
