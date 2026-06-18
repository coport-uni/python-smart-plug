#include "usage_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "usage_led";

#define USAGE_LED_R_GPIO   GPIO_NUM_21
#define USAGE_LED_G_GPIO   GPIO_NUM_38
#define USAGE_LED_B_GPIO   GPIO_NUM_39

/* Long enough to register visually, short enough that the default
 * ~10 s poll cadence never overlaps two pulses. */
#define USAGE_LED_PULSE_US (300 * 1000)

static esp_timer_handle_t s_off_timer;

static void all_off(void)
{
    gpio_set_level(USAGE_LED_R_GPIO, 0);
    gpio_set_level(USAGE_LED_G_GPIO, 0);
    gpio_set_level(USAGE_LED_B_GPIO, 0);
}

static void off_cb(void *arg)
{
    (void)arg;
    all_off();
}

esp_err_t usage_led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << USAGE_LED_R_GPIO) |
                        (1ULL << USAGE_LED_G_GPIO) |
                        (1ULL << USAGE_LED_B_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %d", err);
        return err;
    }
    all_off();

    const esp_timer_create_args_t targs = {
        .callback = off_cb,
        .name     = "usage_led_off",
    };
    err = esp_timer_create(&targs, &s_off_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "init R=%d G=%d B=%d",
             USAGE_LED_R_GPIO, USAGE_LED_G_GPIO, USAGE_LED_B_GPIO);
    return ESP_OK;
}

void usage_led_pulse_orange(void)
{
    if (!s_off_timer) {
        return;
    }
    /* Cancel any in-flight off so back-to-back updates extend the
     * visible pulse instead of truncating it. */
    esp_timer_stop(s_off_timer);
    gpio_set_level(USAGE_LED_R_GPIO, 1);
    gpio_set_level(USAGE_LED_G_GPIO, 1);
    gpio_set_level(USAGE_LED_B_GPIO, 0);
    esp_timer_start_once(s_off_timer, USAGE_LED_PULSE_US);
}
