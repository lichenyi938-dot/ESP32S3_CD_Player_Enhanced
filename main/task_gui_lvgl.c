#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>
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

QueueHandle_t queue_oscilloscope = NULL;

/* ---------- timer interrupt handler ---------- */
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

    /* ---------- create a timer for LVGL ---------- */
    ESP_LOGI("task_lvgl", "config gptimer.");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick = 1us
    };
    gptimer_event_callbacks_t cbs = {
        .on_alarm = gpTimer_alarm_cb,
    };
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 4999,                // 5ms
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    /* ---------- Register LVGL display & draw UI ---------- */
    ESP_LOGI("task_lvgl", "lv_port_disp_init.");
    lv_port_disp_init();

    ESP_LOGI("task_lvgl", "gui_player_init.");
    gui_player_init();

    char str[128];

    while (1)
    {
        /* Drive model */
        snprintf(str, sizeof(str), "%s-%s", cdplayer_driveInfo.vendor, cdplayer_driveInfo.product);
        gui_setDriveModel(str);

        /* Drive/Disc state */
        if (usbhost_driverObj.deviceIsOpened == 0) {
            gui_setDriveState("No drive");
        } else if (!cdplayer_driveInfo.trayClosed) {
            gui_setDriveState("Tray open");
        } else if (!cdplayer_driveInfo.discInserted) {
            gui_setDriveState("No disc");
        } else if (!cdplayer_driveInfo.discIsCD) {
            gui_setDriveState("Not cdda");
        } else {
            gui_setDriveState("Ready");
        }

        /* Volume */
        gui_setVolume(cdplayer_playerInfo.volume);

        if (cdplayer_playerInfo.ready && cdplayer_driveInfo.readyToPlay)
        {
            int8_t trackI = cdplayer_playerInfo.playingTrackIndex;
            if (trackI < 0) trackI = 0;
            if (trackI >= cdplayer_driveInfo.trackCount) {
                trackI = cdplayer_driveInfo.trackCount ? (cdplayer_driveInfo.trackCount - 1) : 0;
            }

            /* Play state */
            if (cdplayer_playerInfo.fastForwarding) {
                gui_setPlayState(">>>");
            } else if (cdplayer_playerInfo.fastBackwarding) {
                gui_setPlayState("<<<");
            } else if (cdplayer_playerInfo.playing) {
                gui_setPlayState(LV_SYMBOL_PLAY);
            } else {
                gui_setPlayState(LV_SYMBOL_PAUSE);
            }

            /* Album (CD-Text) */
            if (cdplayer_driveInfo.cdTextAvailable &&
                (cdplayer_driveInfo.albumTitle[0] != '\0' || cdplayer_driveInfo.albumPerformer[0] != '\0'))
            {
                snprintf(str, sizeof(str), "%s - %s",
                         cdplayer_driveInfo.albumTitle,
                         cdplayer_driveInfo.albumPerformer);
                gui_setAlbumTitle(str);
            } else {
                gui_setAlbumTitle("");
            }

            /* Pre-emphasis */
            gui_setEmphasis(cdplayer_driveInfo.trackList[trackI].preEmphasis);

            /* Track title & performer (CD-Text) */
            if (cdplayer_driveInfo.cdTextAvailable &&
                (cdplayer_driveInfo.trackList[trackI].title[0] != '\0' ||
                 cdplayer_driveInfo.trackList[trackI].performer[0] != '\0'))
            {
                gui_setTrackTitle(cdplayer_driveInfo.trackList[trackI].title,
                                  cdplayer_driveInfo.trackList[trackI].performer);
            } else {
                snprintf(str, sizeof(str), "Track %02d",
                         cdplayer_driveInfo.trackList[trackI].trackNum);
                gui_setTrackTitle(str, "");
            }

            /* Time */
            gui_setTime(cdplay_frameToHmsf(cdplayer_playerInfo.readFrameCount),
                        cdplay_frameToHmsf(cdplayer_driveInfo.trackList[trackI].trackDuration));

            /* Progress */
            gui_setProgress(cdplayer_playerInfo.readFrameCount,
                            cdplayer_driveInfo.trackList[trackI].trackDuration);

            /* Track number */
            gui_setTrackNum(trackI + 1, cdplayer_driveInfo.trackCount);

            /* Oscilloscope & meters */
            static int count = 0;
            static int32_t maxL = 0;
            static int32_t maxR = 0;
            ChannelValue_t osc;

            while (xQueueReceive(queue_oscilloscope, &osc, 0) == pdTRUE)
            {
                int pointCount = ((lv_chart_t *)chart_left)->point_cnt;
                for (int i = 0; i < pointCount - 1; i++) {
                    ser_left->y_points[i]  = ser_left->y_points[i + 1];
                    ser_right->y_points[i] = ser_right->y_points[i + 1];
                }
                ser_left->y_points[pointCount - 1]  = osc.l;
                ser_right->y_points[pointCount - 1] = osc.r;

                if (osc.l > maxL) maxL = osc.l;
                if (osc.r > maxR) maxR = osc.r;

                if ((++count) == 100) {
                    count = 0;
                    lv_chart_refresh(chart_left);
                    lv_chart_refresh(chart_right);

                    double dbL = 20.0 * log10(maxL > 0 ? (double)maxL : 1.0);
                    double dbR = 20.0 * log10(maxR > 0 ? (double)maxR : 1.0);
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
