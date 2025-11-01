#include "cdPlayer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cdplay_stub";

/* ========== 提供 GUI 直接读取的全局信息 ========== */
cdplayer_drive_info_t  cdplayer_driveInfo  = {
    .vendor  = "ESP",
    .product = "USB-ODD",
    .revision= "1.0",
    .cdTextAvailable = true,
    .trackCount = 0, /* 启动时由 init 填充 */
};

cdplayer_player_info_t cdplayer_playerInfo = {
    .volume  = 50,
    .ready   = false,
    .playing = false,
    .readFrameCount = 0,
};

/* ========== 内部桩状态 ========== */
static bool s_ready   = false;
static bool s_playing = false;

/* 小工具：安全字符串赋值 */
static void safe_set(char *dst, const char *src, size_t cap) {
    if (!dst || !cap) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* ========== 初始化/反初始化 ========== */
bool cdplay_init(void)
{
    ESP_LOGI(TAG, "init (stub)");
    s_ready = true;
    s_playing = false;

    /* 构造几首“示例曲目”，方便 GUI 正常显示 */
    cdplayer_driveInfo.trackCount = 3;

    cdplayer_driveInfo.trackList[0].trackNum      = 1;
    cdplayer_driveInfo.trackList[0].preEmphasis   = false;
    safe_set(cdplayer_driveInfo.trackList[0].title,     "Hello, World", sizeof(cdplayer_driveInfo.trackList[0].title));
    safe_set(cdplayer_driveInfo.trackList[0].performer, "ESP-S3",       sizeof(cdplayer_driveInfo.trackList[0].performer));
    cdplayer_driveInfo.trackList[0].trackDuration = 3 * 60 * 75; /* 3:00 */

    cdplayer_driveInfo.trackList[1].trackNum      = 2;
    cdplayer_driveInfo.trackList[1].preEmphasis   = false;
    safe_set(cdplayer_driveInfo.trackList[1].title,     "Demo Track",   sizeof(cdplayer_driveInfo.trackList[1].title));
    safe_set(cdplayer_driveInfo.trackList[1].performer, "Stub Artist",  sizeof(cdplayer_driveInfo.trackList[1].performer));
    cdplayer_driveInfo.trackList[1].trackDuration = 4 * 60 * 75; /* 4:00 */

    cdplayer_driveInfo.trackList[2].trackNum      = 3;
    cdplayer_driveInfo.trackList[2].preEmphasis   = true;
    safe_set(cdplayer_driveInfo.trackList[2].title,     "Pre-Emph Test", sizeof(cdplayer_driveInfo.trackList[2].title));
    safe_set(cdplayer_driveInfo.trackList[2].performer, "Vintage CD",    sizeof(cdplayer_driveInfo.trackList[2].performer));
    cdplayer_driveInfo.trackList[2].trackDuration = 2 * 60 * 75 + 30 * 75 / 60; /* 约 2:30 */

    cdplayer_playerInfo.ready   = s_ready;
    cdplayer_playerInfo.playing = s_playing;
    cdplayer_playerInfo.readFrameCount = 0;

    return true;
}

void cdplay_deinit(void)
{
    ESP_LOGI(TAG, "deinit (stub)");
    s_ready = false;
    s_playing = false;
    cdplayer_playerInfo.ready   = false;
    cdplayer_playerInfo.playing = false;
}

/* 设备初始化（真实项目里可做 USB/SCSI 初始化） */
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

/* ========== 工具：帧 -> H:M:S:F（75fps） ========== */
hmsf_t cdplay_frameToHmsf(uint32_t frames)
{
    hmsf_t t = {0, 0, 0, 0};
    uint32_t total_seconds = frames / 75U;
    t.frame  = frames % 75U;

    t.hour   = total_seconds / 3600U;
    total_seconds %= 3600U;
    t.minute = total_seconds / 60U;
    t.second = total_seconds % 60U;
    return t;
}
