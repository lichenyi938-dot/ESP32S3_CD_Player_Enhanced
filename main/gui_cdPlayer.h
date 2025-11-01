#ifndef __GUI_CD_PLAYER_H_
#define __GUI_CD_PLAYER_H_

﻿#ifndef  cplusplus
exten "c"{
#endif

#ifndef __"lvgl.h"
  #ifndef __<stdbool.h>
  #ifndef __<stdint.h>
  #ifndef __"cdplay.h"  //提供 hmsf t等类型
extern lv_obj_t *area topBar；
extern lv_chart_series_t *area bottomBar
extern lv_obj_t *area player
extern lv_obj_t *area left
extern lv_obj_t *area*ser_right;

extern lv_obj_t *lb_driveModel
extern lv_obj_t *lb_driveState
extern lv_obj_t *lb_alBumTile
extern lv_obj_t *lb_teackTitle
extern lv_obj_t *lb_teackPerformer
extern lv_obj_t *lb_preEmphasized
extern lv_obj_t *lb_time
extern lv_obj_t *lb_duretion

extern lv_obj_t *bar playProgress
extern lv_obj_t *bar meterleft
extern lv_obj_t *bar meterRight

void gui_player_init();

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

#endif
