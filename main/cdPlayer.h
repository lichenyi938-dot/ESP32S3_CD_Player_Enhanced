#ifndef CDPLAYER_H_
#define CDPLAYER_H_

#include <stdbool.h>
#include <stdint.h>

/* CD 帧时间结构：时:分:秒:帧（75帧 = 1秒） */
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t frame;   // 0..74
} hmsf_t;

/* ===== 基础控制接口（桩实现，能编过能跑） ===== */
bool cdplay_init(void);
void cdplay_deinit(void);

/* 设备初始化（比如光驱/USB等），返回是否就绪 */
bool cdplay_devInit(void);

/* 状态查询 */
bool cdplay_isReady(void);
bool cdplay_isPlaying(void);

/* 播放控制 */
void cdplay_play(void);
void cdplay_stop(void);
void cdplay_eject(void);
void cdplay_next(void);
void cdplay_prev(void);

/* 音量控制（0~100） */
void cdplay_setVolume(int vol);
int  cdplay_getVolume(void);

/* 帧数转 H:M:S:F（按红皮书 75fps） */
hmsf_t cdplay_frameToHmsf(uint32_t frames);

#endif /* CDPLAYER_H_ */
