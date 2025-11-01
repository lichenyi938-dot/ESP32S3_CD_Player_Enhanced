/* lv_conf.h - minimal config enabling required fonts for this project */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 让 LVGL 可以 #include "lv_conf.h" */
#define LV_CONF_INCLUDE_SIMPLE 1

/* ---------- LVGL v8 基础配置 ---------- */
#define LV_USE_DRAW_SW        1
#define LV_COLOR_DEPTH        16
#define LV_TICK_CUSTOM        0
#define LV_USE_LOG            0

/* ---------- 本项目需要的字体 ---------- */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_26 1

/* 默认字体设定（与代码一致） */
#define LV_FONT_DEFAULT               &lv_font_montserrat_16
#define LV_THEME_DEFAULT_FONT_SMALL   &lv_font_montserrat_12
#define LV_THEME_DEFAULT_FONT_NORMAL  &lv_font_montserrat_14
#define LV_THEME_DEFAULT_FONT_LARGE   &lv_font_montserrat_26

#endif /* LV_CONF_H */
