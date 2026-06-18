#include "claude_usage.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_http_client.h"

#include "sdkconfig.h"

#include "network.h"
#include "ui.h"
#include "usage_led.h"

static const char *TAG = "claude_usage";

#define BUF_INITIAL        1024
/* Defensive headroom: the server now sends only the header + latest row
 * (~350 B), but keep a generous cap so a misconfigured server that returns
 * the whole growing CSV does not silently truncate the body mid-row. */
#define BUF_MAX            (32 * 1024)
#define HTTP_TIMEOUT_MS    6000
#define TS_DST_CAP         24

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

/* ---------------------- HTTP response buffer ---------------------- */

static esp_err_t buf_ensure(resp_buf_t *r, size_t need)
{
    if (r->cap >= need) {
        return ESP_OK;
    }
    size_t new_cap = r->cap ? r->cap : BUF_INITIAL;
    while (new_cap < need) {
        new_cap *= 2;
        if (new_cap > BUF_MAX) {
            new_cap = BUF_MAX;
            break;
        }
    }
    if (new_cap < need) {
        return ESP_ERR_NO_MEM;
    }
    char *n = realloc(r->buf, new_cap);
    if (!n) {
        return ESP_ERR_NO_MEM;
    }
    r->buf = n;
    r->cap = new_cap;
    return ESP_OK;
}

static esp_err_t http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) {
        return ESP_OK;
    }
    resp_buf_t *r = (resp_buf_t *)evt->user_data;
    size_t need = r->len + (size_t)evt->data_len + 1;
    if (buf_ensure(r, need) != ESP_OK) {
        return ESP_FAIL;
    }
    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += (size_t)evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

/* ---------------------- Parsers ---------------------- */

static int parse_pct(const char *s)
{
    if (!s) {
        return 0;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    int n = 0;
    bool any = false;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
        any = true;
    }
    if (!any) {
        return 0;
    }
    if (n > 100) {
        n = 100;
    }
    return n;
}

/* The CSV holds "X시간 Y분" / "Y분" / "X시간". Walk the UTF-8 bytes,
 * tag digit runs as hours when followed by 시 (EC 8B 9C) and as
 * minutes when followed by 분 (EB B6 84). 간 (EA B0 84) trails 시
 * and is skipped. */
static void parse_kr_time(const char *s, int *h_out, int *m_out)
{
    int h = 0, m = 0;
    if (!s) {
        *h_out = 0;
        *m_out = 0;
        return;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            int n = 0;
            while (*s >= '0' && *s <= '9') {
                n = n * 10 + (*s - '0');
                s++;
            }
            const unsigned char *u = (const unsigned char *)s;
            if (u[0] == 0xEC && u[1] == 0x8B && u[2] == 0x9C) {
                h = n;
                s += 3;
                if ((unsigned char)s[0] == 0xEA &&
                    (unsigned char)s[1] == 0xB0 &&
                    (unsigned char)s[2] == 0x84) {
                    s += 3;
                }
            } else if (u[0] == 0xEB && u[1] == 0xB6 && u[2] == 0x84) {
                m = n;
                s += 3;
            } else {
                if (h == 0 && m == 0) {
                    m = n;
                }
            }
        } else {
            s++;
        }
    }
    *h_out = h;
    *m_out = m;
}

static const char *last_nonempty_line(const char *buf, size_t len)
{
    if (!buf || len == 0) {
        return NULL;
    }
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return NULL;
    }
    size_t i = len;
    while (i > 0 && buf[i - 1] != '\n') {
        i--;
    }
    return buf + i;
}

static bool parse_csv_latest(const char *buf, size_t len,
                             ui_claude_data_t *out,
                             char *ts_dst, size_t ts_cap)
{
    const char *line = last_nonempty_line(buf, len);
    if (!line) {
        return false;
    }

    /* Split the latest line on commas; we need columns 0..3.
     * column 0: timestamp ("2026-05-12 8:34")
     * column 1: session pct  ("5%")
     * column 2: reset time   ("4시간 46분")
     * column 3: weekly all   ("31%") */
    const char *cols[6] = {0};
    int col_count = 1;
    cols[0] = line;
    const char *p = line;
    while (*p && *p != '\n' && col_count < 6) {
        if (*p == ',') {
            p++;
            cols[col_count++] = p;
            continue;
        }
        p++;
    }
    if (col_count < 4) {
        return false;
    }

    /* Skip the header row: a real data row has a digit at column 1. */
    {
        const char *c1 = cols[1];
        while (*c1 == ' ' || *c1 == '\t') {
            c1++;
        }
        if (!(*c1 >= '0' && *c1 <= '9')) {
            return false;
        }
    }

    /* Copy timestamp (column 0). cols[1] - 1 is the comma we split on. */
    {
        size_t n = (size_t)((cols[1] - 1) - cols[0]);
        if (n >= ts_cap) {
            n = ts_cap - 1;
        }
        while (n > 0 && (cols[0][n - 1] == ' ' || cols[0][n - 1] == '\t' ||
                         cols[0][n - 1] == '\r')) {
            n--;
        }
        memcpy(ts_dst, cols[0], n);
        ts_dst[n] = '\0';
    }

    out->timestamp = ts_dst;
    out->session_pct = parse_pct(cols[1]);
    parse_kr_time(cols[2], &out->reset_h, &out->reset_m);
    out->week_all_pct = parse_pct(cols[3]);
    out->valid = true;
    return true;
}

/* ---------------------- Poll cycle ---------------------- */

static void poll_once(void)
{
    resp_buf_t resp = {0};
    esp_http_client_config_t cfg = {
        .url           = CONFIG_CLAUDE_USAGE_SERVER_URL,
        .timeout_ms    = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data     = &resp,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) {
        ui_claude_set_unavailable("http init fail");
        return;
    }
    esp_err_t err = esp_http_client_perform(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET failed: err=%d status=%d", err, status);
        ui_claude_set_unavailable("server unreachable");
        free(resp.buf);
        return;
    }

    ui_claude_data_t d = {0};
    char ts_buf[TS_DST_CAP] = {0};
    if (!parse_csv_latest(resp.buf, resp.len, &d, ts_buf, sizeof(ts_buf))) {
        ESP_LOGW(TAG, "CSV parse failed (header only?) len=%u",
                 (unsigned)resp.len);
        ui_claude_set_unavailable("no CSV data");
        free(resp.buf);
        return;
    }
    ESP_LOGI(TAG, "session=%d%% week=%d%% reset=%dh%02dm ts=%s",
             d.session_pct, d.week_all_pct, d.reset_h, d.reset_m, ts_buf);
    ui_claude_set_data(&d);
    usage_led_pulse_orange();
    free(resp.buf);
}

static void claude_usage_task(void *arg)
{
    (void)arg;
    /* Block until WiFi has an IP, then poll forever. */
    network_wait_connected(portMAX_DELAY);
    while (1) {
        poll_once();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_CLAUDE_USAGE_POLL_INTERVAL_S * 1000));
    }
}

esp_err_t claude_usage_init(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(
        claude_usage_task, "claude_usage", 5120, NULL, 4, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
