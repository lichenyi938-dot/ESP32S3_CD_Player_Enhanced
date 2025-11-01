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

/* ========= 驱动器/唱片信息 ========= */
typedef struct {
    /* 设备字符串 */
    char vendor[16];
    char product[32];
    char revision[8];

    /* 专辑信息 */
    char albumTitle[64];
    char albumPerformer[64];

    /* CD-Text 可用（统一字段） */
    bool cdTextAvailable;   /* 标准字段 */
    bool cdtextAvailable;   /* 兼容旧写法 */
    bool cdfextAvailable;   /* 常见误拼，兼容 */

    /* 连接/介质状态 */
    bool isConnected;       /* 已连接 */
    bool disConnected;      /* 有的代码用相反语义：0 表示已连接 */

    bool trayClosed;        /* 托盘是否关闭 */

    bool discInserted;      /* 已插入光盘 */
    bool discOK;            /* 盘可读/TOC 正常 */

    /* 数据盘标志（有些代码用来区分数据/音频） */
    bool discISO;           /* 常见拼法 */
    bool cdscISO;           /* 误拼别名，保持 */

    /* 是否可以播放（两种拼写） */
    bool readyToPlay;
    bool readToPlay;

    /* 轨道列表 */
    uint8_t  trackCount;
    cdplayer_track_info_t trackList[CDPLAYER_MAX_TRACKS];
} cdplayer_drive_info_t;

/* ========= 播放状态 ========= */
typedef struct {
    int  volume;                 /* 0..100 */
    bool ready;                  /* 设备就绪 */
    bool playing;                /* 播放中 */

    int8_t  playingTrackIndex;   /* -1 表示未知 */
    bool    fastForwarding;
    bool    fastBackwarding;

    uint32_t readFrameCount;     /* 已播放总帧数 */
} cdplayer_player_info_t;

/* 全局量（在 cdPlayer.c 定义） */
extern cdplayer_drive_info_t  cdplayer_driveInfo;
extern cdplayer_player_info_t cdplayer_playerInfo;

/* API（桩/真实实现都可用） */
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

/* ===== 兼容宏：旧代码写错字段名也能编过 ===== */
#ifndef cdtextAvailable
#define cdtextAvailable cdTextAvailable
#endif
#ifndef cdfextAvailable
#define cdfextAvailable cdTextAvailable
#endif

#endif /* CDPLAYER_H_ */
