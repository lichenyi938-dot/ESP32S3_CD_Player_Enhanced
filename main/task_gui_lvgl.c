#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gptimer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#include "main.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "st7789.h"
#include "usbhost_driver.h"
#include "cdPlayer.h"
#include "gui_cdPlayer.h"

/* 兼容：如果别处还写成 cdTextAvalibale，这里给一个别名兜底 */
#ifndef cdTextAvalibale
#define cdTextAvalibale cdTextAvailable
#endif

QueueHandle_t queue_oscilloscope = NULL;

/* 定时器回调 */
static bool IRAM_ATTR gpTimer_alarm_cb(gptimer_handle_t timer,
                                       const gptimer_alarm_event_data_t *edata,
                                       void *user_data)
{
    lv_tick_inc(5);
    return false;
}

void task_lvgl(void *args)
{
    queue_oscilloscope = xQueueCreate(500, sizeof(ChannelValue_t));

    ESP_LOGI("task_lvgl", "lcd_init");
    lcd_init();

    ESP_LOGI("task_lvgl", "lv_init");
    lv_init();

    /* 创建定时器给 LVGL 用 */
    ESP_LOGI("task_lvgl", "config gptimer.");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, /* 1 tick = 1us */
    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = gpTimer_alarm_cb,
    };
    gptimer_alarm_config_t alarm_config2 = {
        .alarm_count = 4999,                /* 5ms */
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config2));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    /* 注册 LVGL 显示设备 */
    ESP_LOGI("task_lvgl", "lv_port_disp_init.");
    lv_port_disp_init();

    /* 绘制界面 */
    ESP_LOGI("task_lvgl", "gui_player_init.");
    gui_player_init();

    /* —— 把缓冲区从 100 扩到 256，避免格式化输出被编译器判为可能截断 —— */
    char str[256];

    while (1)
    {
        /* 光驱型号 */
        snprintf(str, sizeof(str), "%s-%s",
                 cdplayer_driveInfo.vendor, cdplayer_driveInfo.product);
        gui_setDriveModel(str);

        /* 碟状态 */
        if (usbhost_driverObj.deviceIsOpened == 0)
        {
            gui_setDriveState("No drive");
        }
        else if (cdplayer_driveInfo.trayClosed == 0)
        {
            gui_setDriveState("Tray open");
        }
        else if (cdplayer_driveInfo.discInserted == 0)
        {
            gui_setDriveState("No disc");
        }
        else if (cdplayer_driveInfo.discISO) /* 数据盘（不是 CD-DA） */
        {
            gui_setDriveState("Not cdda");
        }
        else
        {
            gui_setDriveState("Ready");
        }

        /* 音量 */
        gui_setVolume(cdplayer_playerInfo.volume);

        if (cdplayer_driveInfo.readyToPlay)
        {
            int8_t trackI = cdplayer_playerInfo.playingTrackIndex;

            /* 播放状态 */
            if (cdplayer_playerInfo.fastForwarding)
                gui_setPlayState(">>>");
            else if (cdplayer_playerInfo.fastBackwarding)
                gui_setPlayState("<<<");
            else if (cdplayer_playerInfo.playing)
                gui_setPlayState(LV_SYMBOL_PLAY);
            else
                gui_setPlayState(LV_SYMBOL_PAUSE);

            /* 专辑名/表演者（CD-Text） */
            if (cdplayer_driveInfo.cdTextAvailable /* 修正拼写 */)
            {
                snprintf(str, sizeof(str), "%s - %s",
                         cdplayer_driveInfo.albumTitle,
                         cdplayer_driveInfo.albumPerformer);
                gui_setAlbumTitle(str);
            }
            else
            {
                gui_setAlbumTitle("");
            }

            /* 预加重 */
            if (trackI >= 0 && trackI < cdplayer_driveInfo.trackCount &&
                cdplayer_driveInfo.trackList[trackI].preEmphasis)
                gui_setEmphasis(true);
            else
                gui_setEmphasis(false);

            /* 曲名/表演者（CD-Text），否则显示 TrackXX */
            if (cdplayer_driveInfo.cdTextAvailable &&
                trackI >= 0 && trackI < cdplayer_driveInfo.trackCount)
            {
                gui_setTrackTitle(
                    cdplayer_driveInfo.trackList[trackI].title,
                    cdplayer_driveInfo.trackList[trackI].performer);
            }
            else
            {
                snprintf(str, sizeof(str), "Track %02d",
                         (trackI >= 0 ? cdplayer_driveInfo.trackList[trackI].trackNum : 0));
                gui_setTrackTitle(str, "");
            }

            /* 播放时间/总时长 */
            uint32_t cur = cdplayer_playerInfo.readFrameCount;
            uint32_t tot = (trackI >= 0 && trackI < cdplayer_driveInfo.trackCount)
                               ? cdplayer_driveInfo.trackList[trackI].trackDuration
                               : 0;
            gui_setTime(cdplay_frameToHmsf(cur), cdplay_frameToHmsf(tot));

            /* 进度条 */
            gui_setProgress(cur, tot);

            /* 轨号/总轨数 */
            gui_setTrackNum(trackI + 1, cdplayer_driveInfo.trackCount);

            /* 示波器 + 电平表 */
            static int count = 0;
            static int32_t maxL = 0;
            static int32_t maxR = 0;
            ChannelValue_t oscilloscopePoint;
            while (xQueueReceive(queue_oscilloscope, &oscilloscopePoint, 0) == pdTRUE)
            {
                int pointCount = ((lv_chart_t *)chart_left)->point_cnt;
                for (int i = 0; i < pointCount - 1; i++)
                {
                    ser_left->y_points[i]  = ser_left->y_points[i + 1];
                    ser_right->y_points[i] = ser_right->y_points[i + 1];
                }
                ser_left->y_points[pointCount - 1]  = oscilloscopePoint.l;
                ser_right->y_points[pointCount - 1] = oscilloscopePoint.r;

                if (oscilloscopePoint.l > maxL) maxL = oscilloscopePoint.l;
                if (oscilloscopePoint.r > maxR) maxR = oscilloscopePoint.r;

                if ((++count) == 100)
                {
                    count = 0;
                    lv_chart_refresh(chart_left);
                    lv_chart_refresh(chart_right);

                    double dbL = 20 * log10(maxL);
                    double dbR = 20 * log10(maxR);
                    maxL = 0;
                    maxR = 0;

                    gui_setMeter(dbL, dbR);
                    break;
                }
            }
        }
        else
        {
            gui_setPlayState(LV_SYMBOL_STOP);
            gui_setAlbumTitle("");
            gui_setEmphasis(false);
            gui_setTrackTitle("(=^_^=)", "");
            gui_setTime(cdplay_frameToHmsf(0), cdplay_frameToHmsf(0));
            gui_setProgress(0, 0);
            gui_setTrackNum(0, 0);
            gui_setMeter(0, 0);
            lv_chart_set_all_value(chart_left,  ser_left,  0);
            lv_chart_set_all_value(chart_right, ser_right, 0);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
