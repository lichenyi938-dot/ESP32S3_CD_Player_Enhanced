/* lv_conf.h - minimal config enabling required fonts for this project */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Use simple include to pick up lv_conf.h from project root */
#define LV_CONF_INCLUDE_SIMPLE 1

/* ---- LVGL version (v8.x) ---- */
#define LV_USE_DRAW_SW        1
#define LV_COLOR_DEPTH        16
#define LV_TICK_CUSTOM        0
#define LV_USE_LOG            0

/* ---- Fonts we actually use in gui_cdPlayer.c ---- */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_26 1

/* Default fonts for theme/widgets */
#define LV_FONT_DEFAULT               &lv_font_montserrat_16
#define LV_THEME_DEFAULT_FONT_SMALL   &lv_font_montserrat_12
#define LV_THEME_DEFAULT_FONT_NORMAL  &lv_font_montserrat_14
#define LV_THEME_DEFAULT_FONT_LARGE   &lv_font_montserrat_26

#endif /* LV_CONF_H */
