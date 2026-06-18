#include "beszel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include "sdkconfig.h"

#include "network.h"
#include "ui.h"

static const char *TAG = "beszel";

#define BUF_INITIAL          4096
#define BUF_MAX              (32 * 1024)
#define HTTP_TIMEOUT_MS      8000
#define TOKEN_LIFETIME_S     (5 * 3600 + 30 * 60)   /* refresh before 6h JWT expiry */
#define TOKEN_MAX            900
#define HOST_NAME_MAX_LEN             32
#define ID_MAX               32

/* Color constants mirror ui.c definitions but use uint32_t so callers do
 * not need lvgl.h. */
#define COL_OK       0x06D6A0u
#define COL_WARN     0xFFD166u
#define COL_PINK     0xEF476Fu
#define COL_MUTED    0x9CA3AFu

typedef struct {
    char     id[ID_MAX];
    char     name[HOST_NAME_MAX_LEN];
    bool     up;
    float    cpu;          /* 0..100 */
    float    mem;          /* 0..100 */
    float    gpu;          /* 0..100 if gpu_present, else NaN */
    bool     gpu_present;
    float    disk;         /* 0..100; Beszel `info.dp`, defaults to 0 if absent */
    uint32_t uptime_s;
} beszel_system_t;

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} resp_buf_t;

static char     s_token[TOKEN_MAX];
static int64_t  s_token_expires_us;

static SemaphoreHandle_t s_mtx;
static beszel_system_t   s_systems[CONFIG_BESZEL_MAX_HOSTS];
static int               s_systems_count;
static int64_t           s_last_ok_us;

static bool s_logged_raw_systems;

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
    if (!r) {
        return ESP_OK;
    }
    if (buf_ensure(r, r->len + evt->data_len + 1) != ESP_OK) {
        ESP_LOGW(TAG, "response buffer hit cap %d", BUF_MAX);
        return ESP_OK;
    }
    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

static void buf_free(resp_buf_t *r)
{
    free(r->buf);
    r->buf = NULL;
    r->len = 0;
    r->cap = 0;
}

/* ---------------------- URL composition ---------------------- */

static void make_url(char *out, size_t out_len, const char *path)
{
    const char *base = CONFIG_BESZEL_SERVER_URL;
    size_t bl = strlen(base);
    /* trim trailing slash */
    while (bl > 0 && base[bl - 1] == '/') {
        bl--;
    }
    snprintf(out, out_len, "%.*s%s", (int)bl, base, path);
}

/* ---------------------- Auth ---------------------- */

static bool token_is_fresh(void)
{
    return s_token[0] != '\0' && esp_timer_get_time() < s_token_expires_us;
}

static esp_err_t beszel_auth(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "identity", CONFIG_BESZEL_USER);
    cJSON_AddStringToObject(root, "password", CONFIG_BESZEL_PASSWORD);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    char url[256];
    make_url(url, sizeof(url),
             "/api/collections/users/auth-with-password");

    resp_buf_t resp = { 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data = &resp,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        free(body);
        return ESP_FAIL;
    }
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body, strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "auth perform: %s", esp_err_to_name(err));
        buf_free(&resp);
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "auth http %d: %s", status,
                 resp.buf ? resp.buf : "(empty)");
        buf_free(&resp);
        return ESP_FAIL;
    }

    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    if (!json) {
        ESP_LOGE(TAG, "auth json parse failed");
        buf_free(&resp);
        return ESP_FAIL;
    }
    cJSON *tok = cJSON_GetObjectItemCaseSensitive(json, "token");
    if (!cJSON_IsString(tok) || !tok->valuestring) {
        ESP_LOGE(TAG, "auth response has no token");
        cJSON_Delete(json);
        buf_free(&resp);
        return ESP_FAIL;
    }
    strncpy(s_token, tok->valuestring, sizeof(s_token) - 1);
    s_token[sizeof(s_token) - 1] = '\0';
    s_token_expires_us = esp_timer_get_time()
                       + (int64_t)TOKEN_LIFETIME_S * 1000000;
    ESP_LOGI(TAG, "auth OK (token len=%u)", (unsigned)strlen(s_token));
    cJSON_Delete(json);
    buf_free(&resp);
    return ESP_OK;
}

/* ---------------------- JSON helpers ---------------------- */

/* Try to read a number from a list of candidate field names, returning
 * the first match. info is an optional nested "info" object — Beszel
 * historically buries metrics there. */
static bool try_get_number(cJSON *root, cJSON *info,
                           const char *const *keys, float *out)
{
    for (int i = 0; keys[i] != NULL; i++) {
        cJSON *n = cJSON_GetObjectItemCaseSensitive(root, keys[i]);
        if (cJSON_IsNumber(n)) {
            *out = (float)n->valuedouble;
            return true;
        }
        if (info) {
            n = cJSON_GetObjectItemCaseSensitive(info, keys[i]);
            if (cJSON_IsNumber(n)) {
                *out = (float)n->valuedouble;
                return true;
            }
        }
    }
    return false;
}

static bool try_get_uint(cJSON *root, cJSON *info,
                         const char *const *keys, uint32_t *out)
{
    float v = 0;
    if (!try_get_number(root, info, keys, &v)) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void parse_one_system(cJSON *item, beszel_system_t *out)
{
    memset(out, 0, sizeof(*out));
    out->gpu = NAN;

    cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        strncpy(out->id, id->valuestring, sizeof(out->id) - 1);
    }
    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (cJSON_IsString(name) && name->valuestring) {
        strncpy(out->name, name->valuestring, sizeof(out->name) - 1);
    } else {
        strncpy(out->name, "?", sizeof(out->name) - 1);
    }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(item, "status");
    out->up = cJSON_IsString(status) && status->valuestring
              && strcmp(status->valuestring, "up") == 0;

    cJSON *info = cJSON_GetObjectItemCaseSensitive(item, "info");
    if (!cJSON_IsObject(info)) {
        info = NULL;
    }

    static const char *cpu_keys[]   = { "cpu", "c", NULL };
    static const char *mem_keys[]   = { "mp", "memory", "mem", "m", NULL };
    static const char *gpu_keys[]   = { "g", "gpu", "gp", NULL };
    static const char *disk_keys[]  = { "dp", "disk", "diskPercent", NULL };
    static const char *up_keys[]    = { "u", "uptime", NULL };

    try_get_number(item, info, cpu_keys, &out->cpu);
    try_get_number(item, info, mem_keys, &out->mem);
    /* Beszel uses `info.g` with json:"omitempty" — a 0% GPU drops the key
     * entirely. We can't distinguish "GPU absent" from "GPU idle", so
     * always show the bar and default a missing key to 0. */
    if (!try_get_number(item, info, gpu_keys, &out->gpu)) {
        out->gpu = 0.0f;
    }
    out->gpu_present = true;
    if (!try_get_number(item, info, disk_keys, &out->disk)) {
        out->disk = 0.0f;
    }
    try_get_uint(item, info, up_keys, &out->uptime_s);
}

/* ---------------------- Fetch + publish ---------------------- */

static void publish_all_to_ui(void)
{
    ui_beszel_host_t local_hosts[CONFIG_BESZEL_MAX_HOSTS];
    char name_buf[CONFIG_BESZEL_MAX_HOSTS][HOST_NAME_MAX_LEN];
    int  count;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    count = s_systems_count;
    for (int i = 0; i < count; i++) {
        strncpy(name_buf[i], s_systems[i].name, sizeof(name_buf[i]) - 1);
        name_buf[i][sizeof(name_buf[i]) - 1] = '\0';
        local_hosts[i].name        = name_buf[i];
        local_hosts[i].up          = s_systems[i].up;
        local_hosts[i].cpu_pct     = (int)(s_systems[i].cpu + 0.5f);
        local_hosts[i].mem_pct     = (int)(s_systems[i].mem + 0.5f);
        local_hosts[i].gpu_present = s_systems[i].gpu_present;
        local_hosts[i].gpu_pct     = s_systems[i].gpu_present
            ? (int)(s_systems[i].gpu + 0.5f) : 0;
        local_hosts[i].disk_pct    = (int)(s_systems[i].disk + 0.5f);
        local_hosts[i].uptime_s    = s_systems[i].uptime_s;
    }
    xSemaphoreGive(s_mtx);

    if (count == 0) {
        ui_beszel_set_unavailable("no hosts");
        return;
    }
    /* active_idx = -1 → ui keeps whichever tab the user currently has
     * selected. Tab navigation is owned by the physical buttons
     * (ui_select_prev_tab / ui_select_next_tab), not by this poller. */
    ui_beszel_replace_hosts(local_hosts, count, -1);
}

static void log_raw_systems(const char *buf, size_t len)
{
    if (s_logged_raw_systems) {
        return;
    }
    s_logged_raw_systems = true;
    ESP_LOGI(TAG, "raw systems response (%u bytes), first chunk follows:",
             (unsigned)len);
    const size_t chunk = 256;
    for (size_t off = 0; off < len; off += chunk) {
        size_t n = (len - off) > chunk ? chunk : (len - off);
        ESP_LOGI(TAG, " %.*s", (int)n, buf + off);
    }
}

static esp_err_t fetch_systems_once(int *out_status)
{
    char url[256];
    make_url(url, sizeof(url),
             "/api/collections/systems/records?perPage=50");

    resp_buf_t resp = { 0 };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event,
        .user_data = &resp,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) {
        return ESP_FAIL;
    }
    char auth_hdr[TOKEN_MAX + 16];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_token);
    esp_http_client_set_header(cli, "Authorization", auth_hdr);

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    *out_status = status;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fetch perform: %s", esp_err_to_name(err));
        buf_free(&resp);
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "fetch http %d", status);
        buf_free(&resp);
        return ESP_FAIL;
    }

    log_raw_systems(resp.buf, resp.len);

    cJSON *json = cJSON_ParseWithLength(resp.buf, resp.len);
    if (!json) {
        ESP_LOGE(TAG, "fetch json parse failed");
        buf_free(&resp);
        return ESP_FAIL;
    }
    cJSON *items = cJSON_GetObjectItemCaseSensitive(json, "items");
    if (!cJSON_IsArray(items)) {
        ESP_LOGE(TAG, "fetch response has no items[]");
        cJSON_Delete(json);
        buf_free(&resp);
        return ESP_FAIL;
    }

    beszel_system_t tmp[CONFIG_BESZEL_MAX_HOSTS];
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, items) {
        if (n >= CONFIG_BESZEL_MAX_HOSTS) {
            break;
        }
        parse_one_system(it, &tmp[n]);
        n++;
    }
    cJSON_Delete(json);
    buf_free(&resp);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    memcpy(s_systems, tmp, sizeof(beszel_system_t) * n);
    s_systems_count = n;
    s_last_ok_us = esp_timer_get_time();
    xSemaphoreGive(s_mtx);

    ESP_LOGI(TAG, "%d systems parsed", n);
    return ESP_OK;
}

static esp_err_t fetch_systems(void)
{
    if (!token_is_fresh()) {
        esp_err_t e = beszel_auth();
        if (e != ESP_OK) {
            return e;
        }
    }
    int status = 0;
    esp_err_t err = fetch_systems_once(&status);
    if (err != ESP_OK && (status == 401 || status == 403)) {
        ESP_LOGI(TAG, "got %d, re-authing once", status);
        s_token[0] = '\0';
        if (beszel_auth() == ESP_OK) {
            err = fetch_systems_once(&status);
        }
    }
    return err;
}

/* ---------------------- Task ---------------------- */

static void beszel_task(void *arg)
{
    (void)arg;

    if (strlen(CONFIG_BESZEL_WIFI_SSID) == 0) {
        ui_beszel_set_unavailable("configure WiFi (menuconfig)");
        vTaskDelete(NULL);
        return;
    }
    if (strlen(CONFIG_BESZEL_USER) == 0 ||
        strlen(CONFIG_BESZEL_PASSWORD) == 0) {
        ui_beszel_set_unavailable("set Beszel user/password");
        vTaskDelete(NULL);
        return;
    }

    ui_beszel_set_status("WiFi connecting...", COL_WARN);
    while (!network_is_connected()) {
        network_wait_connected(2000);
        if (!network_is_connected()) {
            ui_beszel_set_status("WiFi connecting...", COL_WARN);
        }
    }

    const uint32_t period_ms = CONFIG_BESZEL_POLL_INTERVAL_S * 1000;
    int consecutive_fail = 0;

    while (1) {
        if (!network_is_connected()) {
            ui_beszel_set_status("WiFi lost, reconnecting...", COL_WARN);
            network_wait_connected(5000);
            continue;
        }

        esp_err_t err = fetch_systems();
        if (err == ESP_OK) {
            consecutive_fail = 0;
            publish_all_to_ui();
            int64_t now = esp_timer_get_time();
            int age_s = (int)((now - s_last_ok_us) / 1000000);
            char buf[48];
            snprintf(buf, sizeof(buf), "updated %ds ago", age_s);
            ui_beszel_set_status(buf, COL_MUTED);
        } else {
            consecutive_fail++;
            if (s_token[0] == '\0' && consecutive_fail >= 2) {
                ui_beszel_set_status("auth failed (menuconfig)", COL_PINK);
            } else {
                char buf[48];
                int age_s = s_last_ok_us
                    ? (int)((esp_timer_get_time() - s_last_ok_us) / 1000000)
                    : -1;
                if (age_s >= 0) {
                    snprintf(buf, sizeof(buf), "stale %ds", age_s);
                } else {
                    snprintf(buf, sizeof(buf), "fetch failed");
                }
                ui_beszel_set_status(buf, COL_WARN);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

/* ---------------------- Public API ---------------------- */

esp_err_t beszel_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(
        beszel_task, "beszel", 6144, NULL, 4, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}
