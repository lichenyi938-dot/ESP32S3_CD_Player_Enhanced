#ifndef CDPLAYER_H_
#define CDPLAYER_H_

#include <stdbool.h>
#include <stdint.h>

/* ================== 对外可见的数据结构（被 GUI 直接读取） ================== */
typedef struct {
    char vendor[16];     // 厂商，如 "HL-DT-ST"
    char product[32];    // 型号，如 "DVDRAM GT50N"
    char revision[8];    // 固件版本，可选
} cdplayer_drive_info_t;

typedef struct {
    int  volume;         // 0..100
    bool ready;          // 设备是否就绪
    bool playing;        // 是否播放中
} cdplayer_player_info_t;

/* 这两个全局变量是 GUI 直接引用的 */
extern cdplayer_drive_info_t  cdplayer_driveInfo;
extern cdplayer_player_info_t cdplayer_playerInfo;

/* ================== 时间结构 & 工具函数 ================== */
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t frame;   // 0..74（红皮书 75fps）
} hmsf_t;

hmsf_t cdplay_frameToHmsf(uint32_t frames);

/* ================== 播放控制桩接口（后续可填入真实逻辑） ================== */
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
