#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the Beszel polling task.
 *
 * Spawns a background task that, once the network is up, authenticates
 * against the configured Beszel server, fetches the list of monitored
 * systems every CONFIG_BESZEL_POLL_INTERVAL_S seconds and pushes the
 * currently-selected host into the UI via ui_beszel_*.
 *
 * Returns ESP_OK after the task is created, regardless of whether the
 * first poll has happened yet.
 */
esp_err_t beszel_init(void);

#ifdef __cplusplus
}
#endif
