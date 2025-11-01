#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "cdPlayer.h"
#include "gui_cdPlayer.h"

#define AREA_STATUS_BAR_HEIGHT 25
#define AREA_OSCILLOSCOPE_HEIGHT 65
lv_style_t style_preEmphasis;

lv_obj_t *area_topBar;
lv_obj_t *area_bottomBar;
lv_obj_t *area_player;
lv_obj_t *area_left;
lv_obj_t *area_right;

lv_obj_t *lb_driveModel;
lv_obj_t *lb_driveState;
lv_obj_t *lb_albumTitle;
lv_obj_t *lb_trackNumber;
lv_obj_t *lb_trackTitle;
lv_obj_t *lb_trackPerformer;
lv_obj_t *lb_preEmphasized;
lv_obj_t *lb_time;
lv_obj_t *lb_duration;

lv_obj_t *bar_playProgress;
lv_obj_t *bar_meterLeft;
lv_obj_t *bar_meterRight;

lv_obj_t *chart_left;
lv_chart_series_t *ser_left;
lv_obj_t *chart_right;
lv_chart_series_t *ser_right;

lv_obj_t *lb_playState;
lv_obj_t *lb_volume;

void gui_player_init()
{
    lv_color_t color_background = lv_color_make(0x18, 0x18, 0x18);
    lv_color_t color_foreground = lv_color_make(0x1f, 0x1f, 0x1f);

    // main screen
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, color_background, 0);

    /***********************
     * Background - Status Bar
     */
    int size_barWidth = 240;
    int size_barHeight = AREA_STATUS_BAR_HEIGHT;

    // top bar
    static lv_style_t style_topBar;
    lv_style_init(&style_topBar);
    lv_style_set_radius(&style_topBar, 0);
    lv_style_set_bg_opa(&style_topBar, LV_OPA_COVER);
    lv_style_set_bg_color(&style_topBar, color_background);
    lv_style_set_border_width(&style_topBar, 1);
    lv_style_set_border_opa(&style_topBar, LV_OPA_COVER);
    lv_style_set_border_color(&style_topBar, lv_color_make(0x2e, 0x2e, 0x2e));
    lv_style_set_pad_all(&style_topBar, 0);

    area_topBar = lv_obj_create(screen);
    lv_obj_add_style(area_topBar, &style_topBar, 0);
    lv_obj_set_size(area_topBar, size_barWidth, size_barHeight);
    lv_obj_set_pos(area_topBar, 0, 0);
    lv_obj_set_scrollbar_mode(area_topBar, LV_SCROLLBAR_MODE_OFF);

    // bottom bar
    static lv_style_t style_bottomBar;
    lv_style_init(&style_bottomBar);
    lv_style_set_radius(&style_bottomBar, 0);
    lv_style_set_bg_opa(&style_bottomBar, LV_OPA_COVER);
    lv_style_set_bg_color(&style_bottomBar, lv_color_make(0x86, 0x1b, 0x2d))_
