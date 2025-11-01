#ifndef CDPLAYER_H_
#define CDPLAYER_H_

#include <stdbool.h>
#include <stdint.h>

/* ================== 轨道信息 ================== */
#define CDPLAYER_MAX_TRACKS 99   /* 红皮书最多 99 轨 */

typedef struct {
    uint8_t  trackNum;         /* 1..99 */
    bool     preEmphasis;      /* 预加重标识 */
    char     title[64];        /* CD-Text：曲名 */
    char     performer[64];    /* CD-Text：表演者 */
    uint32_t trackDuration;    /* 该轨时长（单位：frame，75fps） */
} cdplayer_track_info_t;

/* ================== 驱动器/唱片信息（GUI 直接读取） ================== */
typedef struct {
    char vendor[16];           /* 厂商，如 "HL-DT-ST" */
    char product[32];          /* 型号，如 "DVDRAM GT50N" */
    char revision[8];          /* 固件版本 */
    bool     cdTextAvailable;  /* 是否有 CD-Text */
    uint8_t  trackCount;       /* 轨道总数 */
    cdplayer_track_info_t trackList[CDPLAYER_MAX_TRACKS];  /* 轨道列表 */
} cdplayer_drive_info_t;

/* ================== 播放状态（GUI 直接读取） ================== */
typedef struct {
    int  volume;               /* 0..100 */
    bool ready;                /* 设备就绪 */
    bool playing;              /* 是否播放中 */
    uint32_t readFrameCount;   /* 当前已播放帧数（75fps） */
} cdplayer_player_info_t;

/* 这两个全局量被 GUI 直接引用 */
extern cdplayer_drive_info_t  cdplayer_driveInfo;
extern cdplayer_player_info_t cdplayer_playerInfo;

/* ================== 时间结构 & 工具 ================== */
typedef struct {
    uint8_t hour, minute, second, frame; /* 0..74 */
} hmsf_t;

hmsf_t cdplay_frameToHmsf(uint32_t frames);

/* ================== 播放控制桩接口 ================== */
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
