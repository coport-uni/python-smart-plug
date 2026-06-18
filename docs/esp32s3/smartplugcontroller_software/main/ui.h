#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    bool        up;
    int         cpu_pct;
    int         mem_pct;
    int         gpu_pct;
    bool        gpu_present;
    int         disk_pct;
    uint32_t    uptime_s;
} ui_beszel_host_t;

typedef struct {
    const char *timestamp;     /* ASCII, e.g. "2026-05-12 8:34" */
    int         session_pct;   /* 0..100, "현재 세션 사용량" */
    int         week_all_pct;  /* 0..100, "주간한도(모든 모델)" */
    int         reset_h;       /* hours, parsed from "X시간 Y분" */
    int         reset_m;       /* minutes */
    bool        valid;         /* false = no usable data yet */
} ui_claude_data_t;

/**
 * Build the LVGL screen. Must be called inside bsp_display_lock /
 * bsp_display_unlock. After this the screen has an empty tabview plus
 * a status footer, ready for ui_beszel_replace_hosts() to populate.
 */
void ui_create(void);

/**
 * Refresh the host tab list. When the host count or any host name
 * differs from the previous call the tabview is rebuilt; otherwise the
 * existing widgets are just updated in place. Pass `active_idx == -1`
 * to leave the currently-active tab alone — the desired behavior when
 * the user, not the poller, owns tab selection.
 */
void ui_beszel_replace_hosts(const ui_beszel_host_t *hosts, int count,
                             int active_idx);

/**
 * Push the latest Claude usage row into the dedicated Claude tab.
 * Safe to call from any task; takes the LVGL lock internally.
 */
void ui_claude_set_data(const ui_claude_data_t *data);

/**
 * Show a "data unavailable" placeholder in the Claude tab when polling
 * fails or the CSV is missing.
 */
void ui_claude_set_unavailable(const char *reason);

/**
 * Advance / retreat the currently-active tab across all tabs (host
 * tabs plus the Claude tab). Wraps at both ends. Intended for the
 * CONFIG / MUTE physical buttons.
 */
void ui_select_prev_tab(void);
void ui_select_next_tab(void);

/**
 * Update the screen-level status footer. `color_hex` uses 0xRRGGBB so
 * callers do not need to include lvgl.h.
 */
void ui_beszel_set_status(const char *msg, uint32_t color_hex);

/**
 * Helper for "no data yet" / "WiFi missing" / "auth failed" states:
 * empties the tabview, writes `reason` into the footer in muted gray.
 */
void ui_beszel_set_unavailable(const char *reason);

#ifdef __cplusplus
}
#endif
