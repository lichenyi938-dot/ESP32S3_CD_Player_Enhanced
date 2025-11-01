#ifndef CDPLAYER_H_
#define CDPLAYER_H_

#include <stdbool.h>
#include <stdint.h>

/* 75 帧 = 1 秒 */
typedef struct {
    uint8_t hour, minute, second, frame;
} hmsf_t;

/* ========= 轨道信息 ========= */
#define CDPLAYER_MAX_TRACKS 99

typedef struct {
    uint8_t  trackNum;           /* 1..99 */
    bool     preEmphasis;        /* 预加重 */
    char     title[64];          /* CD-Text 曲名 */
    char     performer[64];      /* CD-Text 表演者 */
    uint32_t trackDuration;      /* 时长（帧） */
} cdplayer_track_info_t;

/* ========= 驱动器/唱片信息（GUI 会直接读） =========
   说明：为兼容不同分支里出现过的字段名，这里增加了多个“等价字段”。*/
typedef struct {
    /* 设备字符串 */
    char vendor[16];
    char product[32];
    char revision[8];

    /* 专辑/表演者（有些 GUI 会直接显示） */
    char albumTitle[64];
    char albumPerformer[64];

    /* CD-Text 可用：同时提供两种写法以兼容旧代码 */
    bool cdTextAvailable;   /* 正式字段 */
    bool cdtextAvailable;   /* 兼容字段（小写 t） */

    /* 连接/介质状态（不同分支用过不同写法，一次性都给出） */
    bool isConnected;       /* 已连接 */
    bool disConnected;      /* 断开？（某分支用反义词） */

    bool discInserted;      /* 已插入光盘 */
    bool discOK;            /* 盘可读/TOC 正常 */
    bool discISO;           /* 某分支用作“是数据盘”的标志 */
    bool discSC;            /* 兼容保留位，不使用也无妨 */

    /* 是否可以播放（两种拼写都给） */
    bool readyToPlay;
    bool readToPlay;

    /* 轨道列表 */
    uint8_t  trackCount;
    cdplayer_track_info_t trackList[CDPLAYER_MAX_TRACKS];
} cdplayer_drive_info_t;

/* ========= 播放状态（GUI 会直接读） ========= */
typedef struct {
    int  volume;                 /* 0..100 */
    bool ready;                  /* 设备就绪 */
    bool playing;                /* 播放中 */

    /* 当前轨道索引/快进快退状态（GUI 会引用） */
    int8_t playingTrackIndex;    /* -1 表示未知 */
    bool   fastForwarding;
    bool   fastBackwarding;

    /* 已播放的总帧数（用于时间显示） */
    uint32_t readFrameCount;
} cdplayer_player_info_t;

/* 这两个全局量会被 GUI 直接引用 */
extern cdplayer_drive_info_t  cdplayer_driveInfo;
extern cdplayer_player_info_t cdplayer_playerInfo;

/* ========= 工具/控制 API（桩实现，便于编译与 UI 验证） ========= */
hmsf_t cdplay_frameToHmsf(uint32_t frames);

bool cdplay_init(void);
void cdplay_deinit(void);

bool cdplay_devInit(void);

bool cdplay_isReady(void);
bool cdplay_isPlaying(void);

void cdplay_play(void);
void cdplay_stop(void);
void cdplay_eject(void);
void cdplay_next(void);
void cdplay_prev(void);

void cdplay_setVolume(int vol);
int  cdplay_getVolume(void);

#endif /* CDPLAYER_H_ */
