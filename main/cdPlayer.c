#include "cdPlayer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "cdplay_stub";

/* 小工具：安全字符串复制 */
static void safe_set(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* ========= GUI 直接读取的全局对象 ========= */
cdplayer_drive_info_t  cdplayer_driveInfo  = {0};
cdplayer_player_info_t cdplayer_playerInfo = {0};

/* 内部状态 */
static bool s_ready   = false;
static bool s_playing = false;

/* 保持“等价字段”一致性的辅助函数 */
static void sync_drive_flags(void) {
    /* cdTextAvailable / cdtextAvailable 保持一致 */
    cdplayer_driveInfo.cdtextAvailable = cdplayer_driveInfo.cdTextAvailable;

    /* readyToPlay / readToPlay 保持一致 */
    cdplayer_driveInfo.readToPlay  = cdplayer_driveInfo.readyToPlay;

    /* isConnected / disConnected 互为反义（尽量保持合理） */
    cdplayer_driveInfo.isConnected = !cdplayer_driveInfo.disConnected;
}

bool cdplay_init(void)
{
    ESP_LOGI(TAG, "init (stub)");

    /* 连接/介质初始状态 */
    cdplayer_driveInfo.disConnected   = false;
    cdplayer_driveInfo.isConnected    = true;
    cdplayer_driveInfo.discInserted   = true;
    cdplayer_driveInfo.discOK         = true;
    cdplayer_driveInfo.discISO        = false;
    cdplayer_driveInfo.discSC         = false;
    cdplayer_driveInfo.readyToPlay    = true;
    cdplayer_driveInfo.cdTextAvailable= true;

    safe_set(cdplayer_driveInfo.vendor,   "ESP",          sizeof(cdplayer_driveInfo.vendor));
    safe_set(cdplayer_driveInfo.product,  "USB-ODD",      sizeof(cdplayer_driveInfo.product));
    safe_set(cdplayer_driveInfo.revision, "1.0",          sizeof(cdplayer_driveInfo.revision));
    safe_set(cdplayer_driveInfo.albumTitle,     "Demo Album",  sizeof(cdplayer_driveInfo.albumTitle));
    safe_set(cdplayer_driveInfo.albumPerformer, "Demo Artist", sizeof(cdplayer_driveInfo.albumPerformer));

    /* 构造几条示例轨道，方便 UI 联调 */
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

    sync_drive_flags();

    /* 播放器状态 */
    s_ready   = true;
    s_playing = false;

    cdplayer_playerInfo.volume            = 50;
    cdplayer_playerInfo.ready             = s_ready;
    cdplayer_playerInfo.playing           = s_playing;
    cdplayer_playerInfo.readFrameCount    = 0;
    cdplayer_playerInfo.playingTrackIndex = 0;   /* 从第一轨开始 */
    cdplayer_playerInfo.fastForwarding    = false;
    cdplayer_playerInfo.fastBackwarding   = false;

    return true;
}

void cdplay_deinit(void)
{
    ESP_LOGI(TAG, "deinit (stub)");
    s_ready = false;
    s_playing = false;

    cdplayer_playerInfo.ready   = false;
    cdplayer_playerInfo.playing = false;

    cdplayer_driveInfo.disConnected = true;
    cdplayer_driveInfo.isConnected  = false;
    sync_drive_flags();
}

bool cdplay_devInit(void)
{
    ESP_LOGI(TAG, "device init (stub)");
    s_ready = true;
    cdplayer_playerInfo.ready = true;

    cdplayer_driveInfo.disConnected = false;
    cdplayer_driveInfo.isConnected  = true;
    cdplayer_driveInfo.discInserted = true;
    cdplayer_driveInfo.discOK       = true;
    cdplayer_driveInfo.readyToPlay  = true;
    sync_drive_flags();
    return true;
}

/* ===== 状态查询 ===== */
bool cdplay_isReady(void)   { return s_ready; }
bool cdplay_isPlaying(void) { return s_playing; }

/* ===== 播放控制（桩） ===== */
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

void cdplay_eject(void)
{
    /* 模拟弹盘：盘不在，不能播放 */
    cdplayer_driveInfo.discInserted = false;
    cdplayer_driveInfo.discOK       = false;
    cdplayer_driveInfo.readyToPlay  = false;
    sync_drive_flags();
    ESP_LOGI(TAG, "eject (stub)");
}

void cdplay_next(void)
{
    if (cdplayer_playerInfo.playingTrackIndex + 1 < (int)cdplayer_driveInfo.trackCount)
        cdplayer_playerInfo.playingTrackIndex++;
    cdplayer_playerInfo.readFrameCount = 0;
    ESP_LOGI(TAG, "next -> track %d", cdplayer_playerInfo.playingTrackIndex + 1);
}

void cdplay_prev(void)
{
    if (cdplayer_playerInfo.playingTrackIndex > 0)
        cdplayer_playerInfo.playingTrackIndex--;
    cdplayer_playerInfo.readFrameCount = 0;
    ESP_LOGI(TAG, "prev -> track %d", cdplayer_playerInfo.playingTrackIndex + 1);
}

/* ===== 音量 ===== */
void cdplay_setVolume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;
    cdplayer_playerInfo.volume = vol;
    ESP_LOGI(TAG, "set volume = %d", vol);
}

int cdplay_getVolume(void)
{
    return cdplayer_playerInfo.volume;
}

/* ===== 帧 -> H:M:S:F（75fps） ===== */
hmsf_t cdplay_frameToHmsf(uint32_t frames)
{
    hmsf_t t = {0,0,0,0};
    uint32_t secs = frames / 75U;
    t.frame  = frames % 75U;

    t.hour   = secs / 3600U;
    secs    %= 3600U;
    t.minute = secs / 60U;
    t.second = secs % 60U;
    return t;
}
