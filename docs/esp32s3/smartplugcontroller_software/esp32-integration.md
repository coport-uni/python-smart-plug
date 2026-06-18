# Smart Plug HTTP API — ESP32-S3 Integration Guide

This document describes how to control TP-Link Tapo smart plugs from an
**ESP32-S3** over a small HTTP API. The API is served by a FastAPI application
running on the local network; you do not need any TP-Link account or library on
the ESP32 — just plain HTTP requests.

---

## 1. Overview

```
ESP32-S3  ──HTTP (JSON)──►  FastAPI server  ──►  Tapo P110M plugs (Wi-Fi)
          port 17046         (on the LAN)
```

- The server exposes each plug under a friendly **name** (`plug1`, `plug2`, …).
- **POST** requests change a plug's state (on / off / toggle).
- **GET** requests only read (list, state, energy) and never change anything.
- Responses are JSON with a stable, documented shape.

| Item            | Value                                            |
|-----------------|--------------------------------------------------|
| Base URL        | `http://<SERVER_IP>:17046`                       |
| Protocol        | HTTP/1.1, plaintext (no TLS)                      |
| Authentication  | **None** — the API is open on the LAN            |
| Content type    | `application/json` (responses)                   |
| Interactive docs| `http://<SERVER_IP>:17046/docs` (Swagger UI)     |

> Replace `<SERVER_IP>` with the LAN IP of the machine running the server
> (the plugs in this setup are on `192.168.1.x`, so the server host is on the
> same subnet). The server listens on `0.0.0.0:17046`, so any device on the
> same network can reach it.

---

## 2. Network prerequisites

1. The ESP32-S3 and the server must be on the **same Wi-Fi / LAN subnet**.
2. The server must be running and listening on port **17046**.
3. No firewall is blocking TCP `17046` between the ESP32 and the server.

Quick reachability check from any computer on the LAN:

```bash
curl http://<SERVER_IP>:17046/health
# {"status":"ok","device_count":2}
```

---

## 3. API reference

Plug names come from the server's device list; fetch them at runtime with
`GET /plugs` rather than hard-coding. In this deployment the names are
`plug1` and `plug2`.

### 3.1 `GET /health`
Liveness check. Cheap; does **not** contact the plugs.

```json
{ "status": "ok", "device_count": 2 }
```

### 3.2 `GET /plugs`
List the configured plugs. Cheap; does **not** contact the plugs.

```json
[
  { "name": "plug1", "device_type": "tapo p110m",
    "ip": "192.168.1.239", "mac": "18-69-45-71-05-2F" },
  { "name": "plug2", "device_type": "tapo p110m",
    "ip": "192.168.1.79",  "mac": "18-69-45-71-02-7C" }
]
```

### 3.3 `GET /plugs/{name}`
Live on/off state and identity of one plug. **Contacts the device.**

```json
{
  "name": "plug1",
  "is_on": false,
  "model": "P110M",
  "serial": "802216199FFF34CC68F786BADFA995952577CB49",
  "firmware": "1.2.2 Build 240422 Rel.183947",
  "mac": "18:69:45:71:05:2F"
}
```

| Field      | Type            | Notes                          |
|------------|-----------------|--------------------------------|
| `name`     | string          | The plug's name                |
| `is_on`    | bool            | `true` = on, `false` = off     |
| `model`    | string \| null  | e.g. `"P110M"`                 |
| `serial`   | string \| null  | Device unique id               |
| `firmware` | string \| null  | Running firmware version       |
| `mac`      | string \| null  | MAC (colon form)               |

### 3.4 `POST /plugs/{name}/on` · `POST /plugs/{name}/off` · `POST /plugs/{name}/toggle`
Switch a plug. **Contacts the device.** No request body is required (send an
empty body). The response reports the state before and after the action.

```json
{ "name": "plug1", "requested": "on", "before": false, "is_on": true }
```

| Field       | Type           | Notes                                        |
|-------------|----------------|----------------------------------------------|
| `requested` | string         | `"on"`, `"off"`, or `"toggle"`               |
| `before`    | bool \| null   | State observed before the action             |
| `is_on`     | bool \| null   | State observed after the action (the result) |

### 3.5 `GET /plugs/{name}/energy`
Power and energy snapshot. **Contacts the device.**

```json
{
  "name": "plug1",
  "power_w": 91.487,
  "today_kwh": 0.001,
  "month_kwh": 0.001,
  "voltage_v": 220.564,
  "current_a": 0.415
}
```

| Field        | Type           | Unit  | Notes                              |
|--------------|----------------|-------|------------------------------------|
| `power_w`    | number \| null | W     | Instantaneous power                |
| `today_kwh`  | number \| null | kWh   | Energy used today                  |
| `month_kwh`  | number \| null | kWh   | Energy used this month             |
| `voltage_v`  | number \| null | V     | Line voltage                       |
| `current_a`  | number \| null | A     | Line current                       |

### 3.6 Status codes

| Code | Meaning                                   | Body                                       |
|------|-------------------------------------------|--------------------------------------------|
| 200  | Success                                   | the JSON above                             |
| 404  | Unknown plug name                         | `{"detail": "Unknown plug: <name>"}`       |
| 422  | Plug has no energy meter (energy route)   | `{"detail": "Plug has no energy meter: …"}`|
| 502  | Device unreachable / auth / timeout       | `{"detail": "<reason>"}`                   |

On any non-200 response the body is `{"detail": "<message>"}`. Treat `>= 400`
as failure and read `detail` for the reason.

---

## 4. Timing — important for the ESP32 client

- **Per-request latency:** every `/plugs/{name}*` call opens a session to the
  physical plug, so a single request typically takes **~1–3 seconds**. Use a
  generous HTTP timeout (≈10 s) and avoid calling on a tight loop.
- **Energy lags a switch:** the P110M power meter updates **~4–6 seconds after**
  a relay change. If you turn a plug on and immediately read `/energy`, you may
  still see `0 W`. Wait a few seconds before trusting the power reading.
- **Serialize per plug:** the server already serializes concurrent requests to
  the *same* plug, but the client should still avoid firing many overlapping
  requests at one plug.

---

## 5. curl quick test

```bash
BASE=http://<SERVER_IP>:17046

curl $BASE/health
curl $BASE/plugs
curl $BASE/plugs/plug1
curl -X POST $BASE/plugs/plug1/on
sleep 6                                   # let the meter settle
curl $BASE/plugs/plug1/energy
curl -X POST $BASE/plugs/plug1/off
```

---

## 6. Arduino-ESP32 example (ESP32-S3)

**Board:** ESP32-S3 (Arduino core "esp32" by Espressif).
**Libraries:** `WiFi`, `HTTPClient` (bundled with the core) and
[`ArduinoJson`](https://arduinojson.org/) v7 (install via Library Manager).

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---- Configuration -------------------------------------------------------
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";
const char* API_BASE  = "http://192.168.1.50:17046";  // <SERVER_IP>:17046
const uint32_t HTTP_TIMEOUT_MS = 10000;

// ---- Wi-Fi ---------------------------------------------------------------
void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ---- Low-level HTTP helpers ----------------------------------------------
// Returns the HTTP status code; fills `body` with the response text.
int apiRequest(const char* method, const String& path, String& body) {
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(String(API_BASE) + path);

  int code;
  if (strcmp(method, "POST") == 0) {
    code = http.POST("");          // empty body is fine for on/off/toggle
  } else {
    code = http.GET();
  }
  body = (code > 0) ? http.getString() : String();
  http.end();
  return code;
}

// ---- Typed helpers -------------------------------------------------------
// Turn a plug on/off. Returns true on HTTP 200.
bool setPlug(const String& name, bool on) {
  String body;
  int code = apiRequest("POST", "/plugs/" + name + (on ? "/on" : "/off"), body);
  Serial.printf("setPlug(%s, %d) -> %d %s\n",
                name.c_str(), on, code, body.c_str());
  return code == 200;
}

// Read on/off state. Returns true on success and writes into `isOn`.
bool getPlugState(const String& name, bool& isOn) {
  String body;
  int code = apiRequest("GET", "/plugs/" + name, body);
  if (code != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  isOn = doc["is_on"] | false;
  return true;
}

// Read instantaneous power in watts. Returns NAN on failure.
float getPlugPowerW(const String& name) {
  String body;
  int code = apiRequest("GET", "/plugs/" + name + "/energy", body);
  if (code != 200) return NAN;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return NAN;
  return doc["power_w"] | NAN;
}

// ---- Sketch --------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  connectWifi();

  setPlug("plug1", true);          // turn plug1 on
  delay(6000);                     // wait for the energy meter to settle
  float w = getPlugPowerW("plug1");
  Serial.printf("plug1 power: %.1f W\n", w);

  bool on = false;
  if (getPlugState("plug1", on)) {
    Serial.printf("plug1 is %s\n", on ? "ON" : "OFF");
  }

  setPlug("plug1", false);         // turn it back off
}

void loop() {
  // Poll state every 30 s, for example.
  bool on = false;
  if (getPlugState("plug1", on)) {
    Serial.printf("plug1 is %s\n", on ? "ON" : "OFF");
  }
  delay(30000);
}
```

---

## 7. ESP-IDF example (`esp_http_client`)

For projects on bare ESP-IDF instead of Arduino. Assumes Wi-Fi is already
connected (see the IDF `wifi/getting_started` example).

```c
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "plug";

// POST with an empty body — used for /on, /off, /toggle.
static int plug_post(const char *url) {
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_post_field(client, "", 0);   // empty body
    int status = -1;
    if (esp_http_client_perform(client) == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "POST %s -> %d", url, status);
    } else {
        ESP_LOGE(TAG, "POST %s failed", url);
    }
    esp_http_client_cleanup(client);
    return status;
}

void app_main_example(void) {
    // Turn plug1 on. Build full URLs from your server IP.
    plug_post("http://192.168.1.50:17046/plugs/plug1/on");
    // To read state/energy, use HTTP_METHOD_GET and parse the JSON body
    // (e.g. with the bundled cJSON component): fields "is_on", "power_w".
}
```

---

## 8. Troubleshooting

| Symptom                              | Likely cause / fix                                              |
|--------------------------------------|-----------------------------------------------------------------|
| Connection refused / timeout         | Server not running, wrong IP/port, or different subnet/firewall.|
| `404 {"detail":"Unknown plug: …"}`   | Bad plug name — list valid names with `GET /plugs`.             |
| `502 {"detail": …}`                  | The plug itself was unreachable; retry (the server retries once).|
| `/energy` shows `0 W` right after on | Meter not settled yet — wait ~4–6 s and read again.            |
| Slow responses (~seconds)            | Normal: each call talks to the physical plug. Use a 10 s timeout.|

---

## 9. Security note

There is **no authentication**. Anyone who can reach `<SERVER_IP>:17046` can
switch the plugs. Run it only on a trusted LAN, and do not expose port 17046 to
the internet.
