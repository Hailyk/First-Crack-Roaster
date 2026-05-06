#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"

#include "wifi.h"
#include "display.h"
#include "motor.h"

#define AP_SSID "First-Crack"
#define AP_PASSWORD "12345678"

static const char *TAG = "WIFI_MODULE";

static roaster_state_t *g_roaster_state = NULL;
static httpd_handle_t server = NULL;
static esp_event_handler_instance_t wifi_any_id = NULL;
static esp_event_handler_instance_t wifi_got_ip = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

// Configuration portal variables
static char selected_ssid[33] = {0};
static char selected_password[64] = {0};
static bool credentials_received = false;
static SemaphoreHandle_t credentials_mutex = NULL;
static bool connection_successful = false;
static int s_disconnect_retries = 0;
static bool s_recovery_requested = false;
static bool s_recovery_in_progress = false;
static uint8_t s_last_ip_address[4] = {0};
static bool s_has_last_ip_address = false;

static int clamp_drum_value(int value)
{
    if (value > 100) {
        return 100;
    }

    if (value < -100) {
        return -100;
    }

    return value;
}

static float get_drum_cont_pwm(int drum_cont)
{
    // Continuous directional rotation is enabled only outside the deadband.
    if (drum_cont > 40 || drum_cont < -40) {
        return (float)drum_cont;
    }

    return 0.0f;
}

static void display_write_line(uint8_t row, const char *text)
{
    if (text == NULL) {
        return;
    }

    if (set_cursor(row, 0) != ESP_OK) {
        return;
    }

    display_send_data((const uint8_t *)text, strlen(text));
}

static void show_wifi_recovery_screen(void)
{
    char line1[21] = {0};
    char line2[21] = {0};
    char line3[21] = {0};

    snprintf(line1, sizeof(line1), "SSID: %s", AP_SSID);
    snprintf(line2, sizeof(line2), "PWD: %s", AP_PASSWORD);
    snprintf(line3, sizeof(line3), "IP: 192.168.1.1");

    display_send_command(0x01); // Clear display
    vTaskDelay(pdMS_TO_TICKS(100));
    display_send_command(0x02); // Return home
    vTaskDelay(pdMS_TO_TICKS(100));

    display_write_line(0, "WiFi disconnected");
    display_write_line(1, line1);
    display_write_line(2, line2);
    display_write_line(3, line3);
}

static void store_ip_address(const esp_ip4_addr_t *ip_addr)
{
    if (ip_addr == NULL) {
        return;
    }

    s_last_ip_address[0] = ip4_addr1(ip_addr);
    s_last_ip_address[1] = ip4_addr2(ip_addr);
    s_last_ip_address[2] = ip4_addr3(ip_addr);
    s_last_ip_address[3] = ip4_addr4(ip_addr);
    s_has_last_ip_address = true;

    if (g_roaster_state != NULL) {
        memcpy(g_roaster_state->ip_address, s_last_ip_address, sizeof(s_last_ip_address));
    }
}

static void sync_cached_ip_to_state(void)
{
    if (g_roaster_state != NULL && s_has_last_ip_address) {
        memcpy(g_roaster_state->ip_address, s_last_ip_address, sizeof(s_last_ip_address));
    }
}

// HTML page for WiFi configuration
static const char *config_html = 
"<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>WiFi Configuration</title><style>body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;background:#f0f0f0}"
".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
"h1{color:#333;text-align:center;margin-bottom:30px}"
"input[type='text'],input[type='password']{width:100%;padding:12px;margin:10px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px}"
"button{width:100%;padding:15px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin-top:10px}"
"button:hover{background:#45a049}button:disabled{background:#ccc;cursor:not-allowed}"
".status{text-align:center;margin-top:10px;color:#666;font-size:0.9em}"
"label{display:block;margin-top:15px;color:#555;font-weight:bold}"
"</style></head><body><div class='container'><h1>WiFi Configuration</h1>"
"<form onsubmit='return connectWiFi(event)'>"
"<label for='ssid'>WiFi Network Name (SSID)</label>"
"<input type='text' id='ssid' placeholder='Enter WiFi SSID' required/>"
"<label for='password'>Password</label>"
"<input type='password' id='password' placeholder='Enter WiFi password (optional)'/>"
"<button type='submit' id='connect'>Connect</button>"
"</form>"
"<div class='status' id='status'></div></div><script>"
"function connectWiFi(e){e.preventDefault();const ssid=document.getElementById('ssid').value;const pwd=document.getElementById('password').value;"
"if(!ssid){alert('Please enter the WiFi SSID');return false;}"
"document.getElementById('status').textContent='Connecting...';"
"document.getElementById('connect').disabled=true;fetch('/connect',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:pwd})}).then(r=>r.json()).then(data=>{"
"document.getElementById('status').textContent='Connecting to network. Please wait...';}).catch(()=>{"
"document.getElementById('status').textContent='Error. Please try again.';document.getElementById('connect').disabled=false;});return false;}"
"</script></body></html>";

// HTTP handler for config page
static esp_err_t config_page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, config_html, strlen(config_html));
    return ESP_OK;
}

// HTTP handler for WiFi connection request
static esp_err_t connect_handler(httpd_req_t *req) {
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(ssid_json)) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *password_value = "";
    if (password_json != NULL && !cJSON_IsNull(password_json)) {
        if (!cJSON_IsString(password_json)) {
            cJSON_Delete(json);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        password_value = password_json->valuestring;
    }

    if (credentials_mutex != NULL && xSemaphoreTake(credentials_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        strncpy(selected_ssid, ssid_json->valuestring, sizeof(selected_ssid) - 1);
        strncpy(selected_password, password_value, sizeof(selected_password) - 1);
        credentials_received = true;
        xSemaphoreGive(credentials_mutex);
        
        ESP_LOGI(TAG, "Credentials received for SSID: %s", selected_ssid);
    }

    cJSON_Delete(json);

    // Send success response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "success", true);
    char *response_str = cJSON_Print(response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response_str, strlen(response_str));
    
    cJSON_Delete(response);
    free(response_str);
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new client connected");
        return ESP_OK;
    }

    if (g_roaster_state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[128] = {0};
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = buf; // Pre-allocate buffer for receiving data
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ws_pkt.payload[ws_pkt.len < sizeof(buf) - 1 ? ws_pkt.len : sizeof(buf) - 1] = '\0';
    ESP_LOGI(TAG, "Artisan asked: %s", (char *)ws_pkt.payload);

    int request_id = 0;

    cJSON *json = cJSON_Parse((char *)ws_pkt.payload);
    if (json) {
        cJSON *req_id_json = cJSON_GetObjectItemCaseSensitive(json, "MessageID");
        if (cJSON_IsNumber(req_id_json)) {
            request_id = req_id_json->valueint;
        }

        cJSON *air_json = cJSON_GetObjectItemCaseSensitive(json, "air");
        if (cJSON_IsNumber(air_json)) {
            g_roaster_state->air = air_json->valueint;
            ESP_LOGI(TAG, "Updated air to %d", g_roaster_state->air);
        }

        cJSON *burner_json = cJSON_GetObjectItemCaseSensitive(json, "burner");
        if (cJSON_IsNumber(burner_json)) {
            g_roaster_state->burner = burner_json->valueint;
            ESP_LOGI(TAG, "Updated burner to %d", g_roaster_state->burner);
        }

        cJSON *drum_json = cJSON_GetObjectItemCaseSensitive(json, "drum");
        if (cJSON_IsNumber(drum_json)) {
            g_roaster_state->drum = clamp_drum_value(drum_json->valueint);
            g_roaster_state->drum_cont_mode = false;
            ESP_LOGI(TAG, "Updated drum to %d", g_roaster_state->drum);
        }

        cJSON *drum_cont_json = cJSON_GetObjectItemCaseSensitive(json, "drum_cont");
        if (cJSON_IsNumber(drum_cont_json)) {
            g_roaster_state->drum_cont = clamp_drum_value(drum_cont_json->valueint);
            g_roaster_state->drum_cont_mode = true;
            g_roaster_state->motor_pwm = get_drum_cont_pwm(g_roaster_state->drum_cont);

            esp_err_t motor_ret = motor_set_pwm_percent(g_roaster_state->motor_pwm);
            if (motor_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set motor PWM to %.1f%% from drum_cont=%d",
                         g_roaster_state->motor_pwm,
                         g_roaster_state->drum_cont);
            } else {
                ESP_LOGI(TAG, "Updated drum_cont to %d, applied motor PWM %.1f%%",
                         g_roaster_state->drum_cont,
                         g_roaster_state->motor_pwm);
            }
        }

        cJSON_Delete(json);
    }

    char reply_str[256];
    snprintf(reply_str,
             sizeof(reply_str),
             "{\"MessageID\": %d, \"Machine ID\": 0, \"Data\": {\"BT\": %.1f, \"ET\": %.1f, \"air\": %d, \"burner\": %d, \"drum\": %d, \"drum_cont\": %d}}",
             request_id,
             g_roaster_state->bean_temp,
             g_roaster_state->env_temp,
             g_roaster_state->air,
             g_roaster_state->burner,
             g_roaster_state->drum,
             g_roaster_state->drum_cont);

    httpd_ws_frame_t reply_pkt;
    memset(&reply_pkt, 0, sizeof(httpd_ws_frame_t));
    reply_pkt.payload = (uint8_t *)reply_str;
    reply_pkt.len = strlen(reply_str);
    reply_pkt.type = HTTPD_WS_TYPE_TEXT;

    ret = httpd_ws_send_frame(req, &reply_pkt);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Replied with: %s", reply_str);
    }

    return ret;
}

// Initialize WiFi in AP mode for configuration
static esp_err_t wifi_init_ap(void) {
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_ap_netif == NULL) {
        return ESP_FAIL;
    }
    
    // Set static IP for AP
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 1, 1);
    ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 1, 1);
    ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_set_ip_info(s_ap_netif, &ip_info);
    esp_netif_dhcps_start(s_ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s, IP: 192.168.1.1", AP_SSID, AP_PASSWORD);
    return ESP_OK;
}

// Event handler for STA connection attempts
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Connection failed.");
        connection_successful = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "SUCCESS! Connected. IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        store_ip_address(&event->ip_info.ip);
        connection_successful = true;
    }
}

// Try to connect to WiFi in STA mode
static esp_err_t try_wifi_connection(const char *ssid, const char *password) {
    connection_successful = false;
    
    esp_event_handler_instance_t sta_any_id = NULL;
    esp_event_handler_instance_t sta_got_ip = NULL;
    
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    }
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL, &sta_any_id);
    if (ret != ESP_OK) {
        esp_wifi_deinit();
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL, &sta_got_ip);
    if (ret != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_any_id);
        esp_wifi_deinit();
        return ret;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.rssi = -127;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_got_ip);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_any_id);
        esp_wifi_deinit();
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_got_ip);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_any_id);
        esp_wifi_deinit();
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_got_ip);
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_any_id);
        esp_wifi_deinit();
        return ret;
    }

    // Hidden SSIDs may require full-channel scan, so allow a longer connection window.
    for (int i = 0; i < 60; i++) {
        if (connection_successful) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Unregister handlers
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, sta_any_id);

    if (!connection_successful) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Start AP mode with configuration portal and wait for credentials
esp_err_t wifi_start_ap_with_config_portal(char *ssid_out, size_t ssid_size, char *password_out, size_t password_size) {
    if (ssid_out == NULL || password_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    while (true) {  // Loop until successful connection
        // Reset state
        credentials_received = false;
        connection_successful = false;
        memset(selected_ssid, 0, sizeof(selected_ssid));
        memset(selected_password, 0, sizeof(selected_password));

        // Initialize mutex if needed
        if (credentials_mutex == NULL) {
            credentials_mutex = xSemaphoreCreateMutex();
            if (credentials_mutex == NULL) {
                return ESP_ERR_NO_MEM;
            }
        }

        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ret = nvs_flash_erase();
            if (ret != ESP_OK) {
                return ret;
            }
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }

        // Start WiFi in AP mode
        ret = wifi_init_ap();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start AP mode: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Start HTTP server for configuration portal
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        ret = httpd_start(&server, &config);
        if (ret != ESP_OK) {
            esp_wifi_stop();
            esp_wifi_deinit();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Register URI handlers
        httpd_uri_t config_page_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = config_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_page_uri);

        httpd_uri_t connect_uri = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = connect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &connect_uri);

        ESP_LOGI(TAG, "Configuration portal started at http://192.168.1.1");
        show_wifi_recovery_screen();

        // Wait for credentials
        ESP_LOGI(TAG, "Waiting for WiFi credentials...");
        while (!credentials_received) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        // Copy credentials
        if (xSemaphoreTake(credentials_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            strncpy(ssid_out, selected_ssid, ssid_size - 1);
            strncpy(password_out, selected_password, password_size - 1);
            xSemaphoreGive(credentials_mutex);
        }

        ESP_LOGI(TAG, "Credentials received. Attempting to connect to: %s", ssid_out);

        // Stop AP mode and HTTP server
        httpd_stop(server);
        server = NULL;
        esp_wifi_stop();
        esp_wifi_deinit();

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Try to connect to the WiFi network
        ret = try_wifi_connection(ssid_out, password_out);
        
        if (ret == ESP_OK && connection_successful) {
            ESP_LOGI(TAG, "Successfully connected to WiFi!");
            // Clean up mutex
            if (credentials_mutex != NULL) {
                vSemaphoreDelete(credentials_mutex);
                credentials_mutex = NULL;
            }
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "Failed to connect to WiFi. Restarting configuration portal...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            // Loop will restart the AP
        }
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_disconnect_retries = 0;
        s_recovery_in_progress = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_recovery_in_progress) {
            return;
        }

        if (s_disconnect_retries < 10) {
            s_disconnect_retries++;
            esp_wifi_connect();
            ESP_LOGW(TAG, "Disconnected. Retrying connection (%d/10)...", s_disconnect_retries);
        } else {
            if (!s_recovery_requested) {
                s_recovery_requested = true;
                ESP_LOGE(TAG, "Disconnected too many times. restarting wifi configuration portal...");
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_disconnect_retries = 0;
        s_recovery_requested = false;
        s_recovery_in_progress = false;
        ESP_LOGI(TAG, "SUCCESS! Connected. IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        store_ip_address(&event->ip_info.ip);
    }
}

esp_err_t wifi_process_recovery(roaster_state_t *state) {
    if (!s_recovery_requested || s_recovery_in_progress) {
        return ESP_OK;
    }

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_recovery_in_progress = true;
    s_recovery_requested = false;

    show_wifi_recovery_screen();

    char ssid[33] = {0};
    char password[64] = {0};

    esp_err_t ret = wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "wifi_stop during recovery returned: %s", esp_err_to_name(ret));
    }

    ret = wifi_start_ap_with_config_portal(ssid, sizeof(ssid), password, sizeof(password));
    if (ret != ESP_OK) {
        s_recovery_in_progress = false;
        return ret;
    }

    ret = wifi_start(state, ssid, password);
    s_disconnect_retries = 0;
    s_recovery_in_progress = false;
    return ret;
}

esp_err_t wifi_start(roaster_state_t *state, const char *ssid, const char *password) {
    if (state == NULL || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_roaster_state = state;
    sync_cached_ip_to_state();

    // NVS should already be initialized from the AP portal
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    // WiFi is already connected from try_wifi_connection, just register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_any_id);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Warning: Could not register WiFi event handler: %s", esp_err_to_name(ret));
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &wifi_got_ip);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Warning: Could not register IP event handler: %s", esp_err_to_name(ret));
    }

    // Start WebSocket server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret != ESP_OK) {
        httpd_stop(server);
        server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket Server Started!");
    return ESP_OK;
}

esp_err_t wifi_stop(void) {
    esp_err_t ret = ESP_OK;

    if (server != NULL) {
        ret = httpd_stop(server);
        if (ret != ESP_OK) {
            return ret;
        }
        server = NULL;
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        return ret;
    }

    if (wifi_any_id != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_any_id);
        wifi_any_id = NULL;
    }

    if (wifi_got_ip != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_got_ip);
        wifi_got_ip = NULL;
    }

    esp_wifi_deinit();
    esp_netif_deinit();
    s_ap_netif = NULL;
    s_sta_netif = NULL;

    ESP_LOGI(TAG, "WiFi stopped");
    return ESP_OK;
}