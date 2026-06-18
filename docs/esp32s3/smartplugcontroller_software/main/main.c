#include <stdio.h>

#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "ui.h"
#include "buttons_check.h"
#include "network.h"
#include "beszel.h"
#include "claude_usage.h"
#include "usage_led.h"

static const char *TAG = "main";

static void on_config_pressed(void)
{
    ui_select_prev_tab();
}

static void on_mute_pressed(void)
{
    ui_select_next_tab();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Beszel monitor starting");

    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    buttons_callbacks_t btn_cbs = {
        .on_config = on_config_pressed,
        .on_mute   = on_mute_pressed,
    };
    buttons_check_init(&btn_cbs);

    network_init();
    beszel_init();
    ESP_ERROR_CHECK(usage_led_init());
    claude_usage_init();

    ESP_LOGI(TAG, "init complete");
}
