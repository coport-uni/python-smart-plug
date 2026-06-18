#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include "sdkconfig.h"

static const char *TAG = "ui";

#define HOST_NAME_MAX_LEN        32
#define TAB_LABEL_LEN            4   /* host tab bar shows first N chars only */
#define UI_LOCK_MS               50
#define CLAUDE_TS_CAP            24
#define CLAUDE_MSG_CAP           40

#define COLOR_ACCENT    lv_color_hex(0x00E5FF)
#define COLOR_OK        lv_color_hex(0x06D6A0)
#define COLOR_WARN      lv_color_hex(0xFFD166)
#define COLOR_MUTED     lv_color_hex(0x9CA3AF)
#define COLOR_BG        lv_color_hex(0x0A0E27)
#define COLOR_BAR_BG    lv_color_hex(0x1F2937)
#define COLOR_PINK      lv_color_hex(0xEF476F)

typedef struct {
    lv_obj_t *tab_content;
    lv_obj_t *dot_status;
    lv_obj_t *lbl_status_word;
    lv_obj_t *lbl_uptime;
    lv_obj_t *bar_cpu;
    lv_obj_t *lbl_cpu_val;
    lv_obj_t *bar_mem;
    lv_obj_t *lbl_mem_val;
    lv_obj_t *bar_gpu;
    lv_obj_t *lbl_gpu_val;
    lv_obj_t *bar_disk;
    lv_obj_t *lbl_disk_val;
    char      host_name[HOST_NAME_MAX_LEN];
} host_ui_t;

typedef struct {
    lv_obj_t *tab_content;
    lv_obj_t *lbl_timestamp;
    lv_obj_t *bar_session;
    lv_obj_t *lbl_session_val;
    lv_obj_t *bar_week;
    lv_obj_t *lbl_week_val;
    lv_obj_t *lbl_reset;
} claude_ui_t;

static lv_obj_t   *s_tabview;
static lv_obj_t   *s_lbl_status;
static host_ui_t   s_host_ui[CONFIG_BESZEL_MAX_HOSTS];
static int         s_host_ui_count;

static claude_ui_t s_claude_ui;
/* Claude data cached so that a tabview rebuild can re-render it without
 * waiting for the next poll. */
static bool        s_claude_have_data;
static bool        s_claude_show_placeholder;
static int         s_claude_session_pct;
static int         s_claude_week_pct;
static int         s_claude_reset_h;
static int         s_claude_reset_m;
static char        s_claude_ts[CLAUDE_TS_CAP];
static char        s_claude_msg[CLAUDE_MSG_CAP];

#define UI_WITH_LOCK(BLOCK)                          \
    do {                                             \
        if (bsp_display_lock(UI_LOCK_MS)) {          \
            BLOCK;                                   \
            bsp_display_unlock();                    \
        }                                            \
    } while (0)

/* ---------------------- helpers ---------------------- */

static int clamp_pct(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 100) {
        return 100;
    }
    return v;
}

/* Cyan up to 70%, yellow 70-89%, pink at 90%+. Threshold change driven by
 * user preference — flag pressure on any resource (CPU/MEM/GPU/DISK). */
static lv_color_t bar_color_for_pct(int pct)
{
    if (pct >= 90) {
        return COLOR_PINK;
    }
    if (pct >= 70) {
        return COLOR_WARN;
    }
    return COLOR_ACCENT;
}

static void format_uptime(uint32_t seconds, char *buf, size_t cap)
{
    unsigned d = (unsigned)(seconds / 86400);
    unsigned h = (unsigned)((seconds % 86400) / 3600);
    unsigned m = (unsigned)((seconds % 3600) / 60);
    if (d > 0) {
        snprintf(buf, cap, "Up %ud %uh", d, h);
    } else if (h > 0) {
        snprintf(buf, cap, "Up %uh %um", h, m);
    } else {
        snprintf(buf, cap, "Up %um", m);
    }
}

/* ---------------------- per-host tab build ---------------------- */

static void build_metric_row(lv_obj_t *tab, const char *label,
                             int y, lv_obj_t **bar_out, lv_obj_t **val_out)
{
    lv_obj_t *lbl = lv_label_create(tab);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, COLOR_MUTED, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *bar = lv_bar_create(tab);
    lv_obj_set_size(bar, 200, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 40, y + 2);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, COLOR_BAR_BG, 0);
    lv_obj_set_style_bg_color(bar, COLOR_ACCENT, LV_PART_INDICATOR);
    *bar_out = bar;

    lv_obj_t *val = lv_label_create(tab);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_black(), 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 246, y);
    *val_out = val;
}

static void build_host_tab(lv_obj_t *tab, host_ui_t *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->tab_content = tab;

    ui->dot_status = lv_obj_create(tab);
    lv_obj_set_size(ui->dot_status, 12, 12);
    lv_obj_set_style_radius(ui->dot_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui->dot_status, 0, 0);
    lv_obj_set_style_pad_all(ui->dot_status, 0, 0);
    lv_obj_set_style_bg_color(ui->dot_status, COLOR_MUTED, 0);
    lv_obj_align(ui->dot_status, LV_ALIGN_TOP_LEFT, 0, 4);
    lv_obj_clear_flag(ui->dot_status, LV_OBJ_FLAG_SCROLLABLE);

    ui->lbl_status_word = lv_label_create(tab);
    lv_label_set_text(ui->lbl_status_word, "--");
    lv_obj_set_style_text_color(ui->lbl_status_word, COLOR_MUTED, 0);
    lv_obj_align(ui->lbl_status_word, LV_ALIGN_TOP_LEFT, 20, 0);

    ui->lbl_uptime = lv_label_create(tab);
    lv_label_set_text(ui->lbl_uptime, "");
    lv_obj_set_style_text_color(ui->lbl_uptime, COLOR_MUTED, 0);
    lv_obj_align(ui->lbl_uptime, LV_ALIGN_TOP_RIGHT, 0, 0);

    build_metric_row(tab, "CPU",  30, &ui->bar_cpu,  &ui->lbl_cpu_val);
    build_metric_row(tab, "MEM",  60, &ui->bar_mem,  &ui->lbl_mem_val);
    build_metric_row(tab, "GPU",  90, &ui->bar_gpu,  &ui->lbl_gpu_val);
    build_metric_row(tab, "DISK", 120, &ui->bar_disk, &ui->lbl_disk_val);
}

static void apply_host_data(host_ui_t *ui, const ui_beszel_host_t *h)
{
    int cpu  = clamp_pct(h->cpu_pct);
    int mem  = clamp_pct(h->mem_pct);
    int gpu  = clamp_pct(h->gpu_pct);
    int disk = clamp_pct(h->disk_pct);

    lv_obj_set_style_bg_color(ui->dot_status,
                              h->up ? COLOR_OK : COLOR_PINK, 0);
    lv_label_set_text(ui->lbl_status_word, h->up ? "UP" : "DOWN");
    lv_obj_set_style_text_color(ui->lbl_status_word,
                                h->up ? COLOR_OK : COLOR_PINK, 0);

    char ub[32];
    format_uptime(h->uptime_s, ub, sizeof(ub));
    lv_label_set_text(ui->lbl_uptime, ub);

    lv_obj_set_style_bg_color(ui->bar_cpu, bar_color_for_pct(cpu),
                              LV_PART_INDICATOR);
    lv_bar_set_value(ui->bar_cpu, cpu, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui->lbl_cpu_val, "%d%%", cpu);

    lv_obj_set_style_bg_color(ui->bar_mem, bar_color_for_pct(mem),
                              LV_PART_INDICATOR);
    lv_bar_set_value(ui->bar_mem, mem, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui->lbl_mem_val, "%d%%", mem);

    if (h->gpu_present) {
        lv_obj_set_style_bg_color(ui->bar_gpu, bar_color_for_pct(gpu),
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_gpu, gpu, LV_ANIM_OFF);
        lv_label_set_text_fmt(ui->lbl_gpu_val, "%d%%", gpu);
    } else {
        lv_obj_set_style_bg_color(ui->bar_gpu, COLOR_MUTED,
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_gpu, 0, LV_ANIM_OFF);
        lv_label_set_text(ui->lbl_gpu_val, "N/A");
    }

    lv_obj_set_style_bg_color(ui->bar_disk, bar_color_for_pct(disk),
                              LV_PART_INDICATOR);
    lv_bar_set_value(ui->bar_disk, disk, LV_ANIM_OFF);
    lv_label_set_text_fmt(ui->lbl_disk_val, "%d%%", disk);
}

/* ---------------------- Claude tab build ---------------------- */

static void build_claude_tab(lv_obj_t *tab, claude_ui_t *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->tab_content = tab;

    /* Top: muted "Updated YYYY-MM-DD HH:MM" timestamp. */
    ui->lbl_timestamp = lv_label_create(tab);
    lv_label_set_text(ui->lbl_timestamp, "waiting for CSV...");
    lv_obj_set_style_text_color(ui->lbl_timestamp, COLOR_MUTED, 0);
    lv_obj_align(ui->lbl_timestamp, LV_ALIGN_TOP_LEFT, 0, 0);

    /* SESSION row: label + bar + percent. Label is wider than the host
     * labels ("CPU"/"MEM") so the bar starts further right. */
    lv_obj_t *lbl_s = lv_label_create(tab);
    lv_label_set_text(lbl_s, "SESSION");
    lv_obj_set_style_text_color(lbl_s, COLOR_MUTED, 0);
    lv_obj_align(lbl_s, LV_ALIGN_TOP_LEFT, 0, 30);

    ui->bar_session = lv_bar_create(tab);
    lv_obj_set_size(ui->bar_session, 170, 14);
    lv_obj_align(ui->bar_session, LV_ALIGN_TOP_LEFT, 70, 32);
    lv_bar_set_range(ui->bar_session, 0, 100);
    lv_obj_set_style_bg_color(ui->bar_session, COLOR_BAR_BG, 0);
    lv_obj_set_style_bg_color(ui->bar_session, COLOR_ACCENT,
                              LV_PART_INDICATOR);

    ui->lbl_session_val = lv_label_create(tab);
    lv_label_set_text(ui->lbl_session_val, "--");
    lv_obj_set_style_text_color(ui->lbl_session_val, lv_color_black(), 0);
    lv_obj_align(ui->lbl_session_val, LV_ALIGN_TOP_LEFT, 246, 30);

    /* WEEK row. */
    lv_obj_t *lbl_w = lv_label_create(tab);
    lv_label_set_text(lbl_w, "WEEK");
    lv_obj_set_style_text_color(lbl_w, COLOR_MUTED, 0);
    lv_obj_align(lbl_w, LV_ALIGN_TOP_LEFT, 0, 60);

    ui->bar_week = lv_bar_create(tab);
    lv_obj_set_size(ui->bar_week, 170, 14);
    lv_obj_align(ui->bar_week, LV_ALIGN_TOP_LEFT, 70, 62);
    lv_bar_set_range(ui->bar_week, 0, 100);
    lv_obj_set_style_bg_color(ui->bar_week, COLOR_BAR_BG, 0);
    lv_obj_set_style_bg_color(ui->bar_week, COLOR_ACCENT,
                              LV_PART_INDICATOR);

    ui->lbl_week_val = lv_label_create(tab);
    lv_label_set_text(ui->lbl_week_val, "--");
    lv_obj_set_style_text_color(ui->lbl_week_val, lv_color_black(), 0);
    lv_obj_align(ui->lbl_week_val, LV_ALIGN_TOP_LEFT, 246, 60);

    /* Reset-in row: prominent accent text, centered. */
    ui->lbl_reset = lv_label_create(tab);
    lv_label_set_text(ui->lbl_reset, "Reset in --");
    lv_obj_set_style_text_color(ui->lbl_reset, COLOR_ACCENT, 0);
    lv_obj_set_style_text_align(ui->lbl_reset, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ui->lbl_reset, 300);
    lv_obj_align(ui->lbl_reset, LV_ALIGN_TOP_LEFT, 10, 110);
}

static void apply_claude_data_widgets(claude_ui_t *ui)
{
    int s = clamp_pct(s_claude_session_pct);
    int w = clamp_pct(s_claude_week_pct);

    if (s_claude_show_placeholder) {
        lv_label_set_text(ui->lbl_timestamp,
                          s_claude_msg[0] ? s_claude_msg : "unavailable");
        lv_obj_set_style_bg_color(ui->bar_session, COLOR_MUTED,
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_session, 0, LV_ANIM_OFF);
        lv_label_set_text(ui->lbl_session_val, "--");

        lv_obj_set_style_bg_color(ui->bar_week, COLOR_MUTED,
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_week, 0, LV_ANIM_OFF);
        lv_label_set_text(ui->lbl_week_val, "--");

        lv_obj_set_style_text_color(ui->lbl_reset, COLOR_MUTED, 0);
        lv_label_set_text(ui->lbl_reset, "Reset in --");
        return;
    }

    if (s_claude_have_data) {
        lv_label_set_text_fmt(ui->lbl_timestamp, "Updated %s", s_claude_ts);

        lv_obj_set_style_bg_color(ui->bar_session, bar_color_for_pct(s),
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_session, s, LV_ANIM_OFF);
        lv_label_set_text_fmt(ui->lbl_session_val, "%d%%", s);

        lv_obj_set_style_bg_color(ui->bar_week, bar_color_for_pct(w),
                                  LV_PART_INDICATOR);
        lv_bar_set_value(ui->bar_week, w, LV_ANIM_OFF);
        lv_label_set_text_fmt(ui->lbl_week_val, "%d%%", w);

        lv_obj_set_style_text_color(ui->lbl_reset, COLOR_ACCENT, 0);
        lv_label_set_text_fmt(ui->lbl_reset, "Reset in %dh %02dm",
                              s_claude_reset_h, s_claude_reset_m);
    } else {
        lv_label_set_text(ui->lbl_timestamp, "waiting for CSV...");
        lv_label_set_text(ui->lbl_reset, "Reset in --");
    }
}

/* ---------------------- tabview lifecycle ---------------------- */

static bool topology_changed(const ui_beszel_host_t *hosts, int count)
{
    if (count != s_host_ui_count) {
        return true;
    }
    for (int i = 0; i < count; i++) {
        const char *new_name = hosts[i].name ? hosts[i].name : "";
        if (strncmp(new_name, s_host_ui[i].host_name,
                    sizeof(s_host_ui[i].host_name)) != 0) {
            return true;
        }
    }
    return false;
}

static void create_empty_tabview(void)
{
    lv_obj_t *scr = lv_scr_act();
    s_tabview = lv_tabview_create(scr);
    lv_obj_set_size(s_tabview, 320, 220);
    lv_obj_align(s_tabview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_tabview_set_tab_bar_size(s_tabview, 30);
}

/* Append the always-present Claude tab and re-render whatever Claude
 * state we have cached. Used by rebuild_tabview and ui_create. */
static void append_claude_tab(void)
{
    lv_obj_t *claude_tab = lv_tabview_add_tab(s_tabview, "Claude");
    build_claude_tab(claude_tab, &s_claude_ui);
    apply_claude_data_widgets(&s_claude_ui);
}

static void rebuild_tabview(const ui_beszel_host_t *hosts, int count)
{
    if (s_tabview) {
        lv_obj_delete(s_tabview);
        s_tabview = NULL;
    }
    memset(s_host_ui, 0, sizeof(s_host_ui));
    s_host_ui_count = 0;
    memset(&s_claude_ui, 0, sizeof(s_claude_ui));

    create_empty_tabview();

    int n = count;
    if (n > CONFIG_BESZEL_MAX_HOSTS) {
        n = CONFIG_BESZEL_MAX_HOSTS;
    }
    for (int i = 0; i < n; i++) {
        const char *name = hosts[i].name ? hosts[i].name : "?";
        /* The tab bar only shows the first TAB_LABEL_LEN chars so several
         * PCs fit across the 320px bar. The full name still lives in
         * host_name for topology comparison, so two PCs sharing a 4-char
         * prefix are not mistaken for one another. */
        char label[TAB_LABEL_LEN + 1];
        snprintf(label, sizeof(label), "%.*s", TAB_LABEL_LEN, name);
        lv_obj_t *tab = lv_tabview_add_tab(s_tabview, label);
        build_host_tab(tab, &s_host_ui[i]);
        strncpy(s_host_ui[i].host_name, name,
                sizeof(s_host_ui[i].host_name) - 1);
    }
    s_host_ui_count = n;

    append_claude_tab();

    /* Pull the status footer back above the tabview so it stays clickable
     * (lv_obj_delete + create reset z-order). */
    if (s_lbl_status) {
        lv_obj_move_foreground(s_lbl_status);
    }
}

/* ---------------------- public API ---------------------- */

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

    create_empty_tabview();
    append_claude_tab();

    s_lbl_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_status, "starting...");
    lv_obj_set_style_text_color(s_lbl_status, COLOR_MUTED, 0);
    lv_obj_set_width(s_lbl_status, 320);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_status, LV_ALIGN_BOTTOM_LEFT, 4, -2);

    ESP_LOGI(TAG, "ui ready");
}

void ui_beszel_replace_hosts(const ui_beszel_host_t *hosts, int count,
                             int active_idx)
{
    if (!hosts || count < 0) {
        return;
    }
    UI_WITH_LOCK({
        if (topology_changed(hosts, count)) {
            rebuild_tabview(hosts, count);
        }
        int n = count;
        if (n > CONFIG_BESZEL_MAX_HOSTS) {
            n = CONFIG_BESZEL_MAX_HOSTS;
        }
        for (int i = 0; i < n; i++) {
            apply_host_data(&s_host_ui[i], &hosts[i]);
        }
        /* active_idx >= 0 forces a tab change; -1 means "leave whichever
         * tab the user is on alone". */
        if (active_idx >= 0 && s_tabview) {
            uint32_t total = lv_tabview_get_tab_count(s_tabview);
            if (total > 0) {
                uint32_t idx = (uint32_t)active_idx;
                if (idx >= total) {
                    idx = total - 1;
                }
                lv_tabview_set_active(s_tabview, idx, LV_ANIM_OFF);
            }
        }
    });
}

void ui_claude_set_data(const ui_claude_data_t *data)
{
    if (!data) {
        return;
    }
    UI_WITH_LOCK({
        s_claude_have_data = data->valid;
        s_claude_show_placeholder = false;
        s_claude_session_pct = data->session_pct;
        s_claude_week_pct = data->week_all_pct;
        s_claude_reset_h = data->reset_h;
        s_claude_reset_m = data->reset_m;
        if (data->timestamp) {
            strncpy(s_claude_ts, data->timestamp, sizeof(s_claude_ts) - 1);
            s_claude_ts[sizeof(s_claude_ts) - 1] = '\0';
        } else {
            s_claude_ts[0] = '\0';
        }
        s_claude_msg[0] = '\0';
        if (s_claude_ui.lbl_reset) {
            apply_claude_data_widgets(&s_claude_ui);
        }
    });
}

void ui_claude_set_unavailable(const char *reason)
{
    UI_WITH_LOCK({
        s_claude_show_placeholder = true;
        if (reason) {
            strncpy(s_claude_msg, reason, sizeof(s_claude_msg) - 1);
            s_claude_msg[sizeof(s_claude_msg) - 1] = '\0';
        } else {
            s_claude_msg[0] = '\0';
        }
        if (s_claude_ui.lbl_reset) {
            apply_claude_data_widgets(&s_claude_ui);
        }
    });
}

void ui_select_prev_tab(void)
{
    UI_WITH_LOCK({
        if (s_tabview) {
            uint32_t cnt = lv_tabview_get_tab_count(s_tabview);
            if (cnt > 0) {
                uint32_t cur = lv_tabview_get_tab_active(s_tabview);
                uint32_t nxt = (cur + cnt - 1) % cnt;
                lv_tabview_set_active(s_tabview, nxt, LV_ANIM_OFF);
            }
        }
    });
}

void ui_select_next_tab(void)
{
    UI_WITH_LOCK({
        if (s_tabview) {
            uint32_t cnt = lv_tabview_get_tab_count(s_tabview);
            if (cnt > 0) {
                uint32_t cur = lv_tabview_get_tab_active(s_tabview);
                uint32_t nxt = (cur + 1) % cnt;
                lv_tabview_set_active(s_tabview, nxt, LV_ANIM_OFF);
            }
        }
    });
}

void ui_beszel_set_status(const char *msg, uint32_t color_hex)
{
    UI_WITH_LOCK({
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status, msg ? msg : "");
            lv_obj_set_style_text_color(s_lbl_status,
                                        lv_color_hex(color_hex), 0);
        }
    });
}

void ui_beszel_set_unavailable(const char *reason)
{
    UI_WITH_LOCK({
        if (s_tabview && s_host_ui_count > 0) {
            lv_obj_delete(s_tabview);
            s_tabview = NULL;
            memset(s_host_ui, 0, sizeof(s_host_ui));
            s_host_ui_count = 0;
            memset(&s_claude_ui, 0, sizeof(s_claude_ui));
            create_empty_tabview();
            append_claude_tab();
            if (s_lbl_status) {
                lv_obj_move_foreground(s_lbl_status);
            }
        }
        if (s_lbl_status) {
            lv_label_set_text(s_lbl_status,
                              reason ? reason : "unavailable");
            lv_obj_set_style_text_color(s_lbl_status, COLOR_MUTED, 0);
        }
    });
}
