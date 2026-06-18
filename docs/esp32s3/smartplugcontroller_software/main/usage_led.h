#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Configure the external common-cathode RGB LED used as the
 *         "Claude usage updated" indicator.
 *
 * Brings up GPIO 21 (R), GPIO 38 (G) and GPIO 39 (B) as push-pull outputs
 * driven low, and creates the one-shot esp_timer that turns the pulse off
 * asynchronously. Must be called from app_main before any task can call
 * usage_led_pulse_orange().
 *
 * @return ESP_OK on success, or the first error returned by gpio_config /
 *         esp_timer_create.
 */
esp_err_t usage_led_init(void);

/**
 * @brief  Briefly light the indicator LED orange.
 *
 * Drives R and G high (B stays low) for ~300 ms, then turns all channels
 * off via a one-shot timer so the caller never blocks. Repeated calls
 * within the pulse window restart the off timer rather than truncating
 * the pulse. Safe to call from any task once usage_led_init() has run.
 */
void      usage_led_pulse_orange(void);

#ifdef __cplusplus
}
#endif
