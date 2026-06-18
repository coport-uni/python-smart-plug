#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the Claude-usage CSV polling task.
 *
 * Spawns a background task that, once the network is up, fetches
 * CONFIG_CLAUDE_USAGE_SERVER_URL every CONFIG_CLAUDE_USAGE_POLL_INTERVAL_S
 * seconds, parses the latest CSV row and pushes session / reset / weekly
 * fields into the dedicated "Claude" LVGL tab via ui_claude_*.
 *
 * Returns ESP_OK once the task is created.
 */
esp_err_t claude_usage_init(void);

#ifdef __cplusplus
}
#endif
