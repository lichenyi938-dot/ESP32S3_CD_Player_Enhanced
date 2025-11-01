#ifndef GUI_CDPLAYER_H_
#define GUI_CDPLAYER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"        // ✅ 必须放在最前，提供 lv_obj_t / lv_chart_series_t
#include <stdbool.h>
#include <stdint.h>
#include "cdPlayer.h"    // 提供 hmsf_t 等类型

/* ===== 全局控件指针（与 gui_cdPlayer.c 中的定义对应） ===== */
extern lv_obj_t *area_topBar;
extern lv_obj_t *area_bottomBar;
extern lv_obj_t *area_player;
extern lv_obj_t *area_left;
extern lv_obj_t *area_right;

extern lv_obj_t *lb_driveModel;
extern lv_obj_t *lb_driveState;
extern lv_obj_t *lb_albumTitle;
extern lv_obj_t *lb_trackNumber;
extern lv_obj_t *lb_trackTitle;
extern lv_obj_t *lb_trackPerformer;
extern lv_obj_t *lb_preEmphasized;
extern lv_obj_t *lb_time;
extern lv_obj_t *lb_duration;

extern lv_obj_t *bar_playProgress;
extern lv_obj_t *bar_meterLeft;
extern lv_obj_t *bar_meterRight;

extern lv_obj_t *chart_left;
extern lv_chart_series_t *ser_left;
extern lv_obj_t *chart_right;
extern lv_chart_series_t *ser_right;

extern lv_obj_t *lb_playState;
extern lv_obj_t *lb_volume;

/* ===== 对外接口 ===== */
void gui_player_init(void);
void gui_setDriveModel(const char *str);
void gui_setDriveState(const char *str);
void gui_setAlbumTitle(const char *str);
void gui_setTrackTitle(const char *title, const char *performer);
void gui_setEmphasis(bool en);
void gui_setTime(hmsf_t current, hmsf_t total);
void gui_setProgress(uint32_t current, uint32_t total);
void gui_setPlayState(const char *str);
void gui_setVolume(int vol);
void gui_setTrackNum(int current, int total);
void gui_setMeter(int l, int r);

#ifdef __cplusplus
}
#endif

#endif /* GUI_CDPLAYER_H_ */
