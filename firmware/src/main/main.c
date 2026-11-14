#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

#define EXAMPLE_ESP_WIFI_SSID      "Robot_Omni_AP"
#define EXAMPLE_ESP_WIFI_PASS      "12345678"
#define EXAMPLE_ESP_WIFI_CHANNEL   1
#define EXAMPLE_MAX_STA_CONN       4

// Definición de Pines para los 4 Motores (Independientes)
// Driver 1 (Lado Izquierdo)
#define FL_IN1     16  // Front Left
#define FL_IN2     17
#define RL_IN1     18  // Rear Left
#define RL_IN2     19

// Driver 2 (Lado Derecho)
#define FR_IN1     21  // Front Right
#define FR_IN2     22
#define RR_IN1     23  // Rear Right
#define RR_IN2     25

static const char *TAG = "omni_robot";

void motors_init(void) {
    uint64_t pin_mask = (1ULL << FL_IN1) | (1ULL << FL_IN2) |
                          (1ULL << RL_IN1) | (1ULL << RL_IN2) |
                          (1ULL << FR_IN1) | (1ULL << FR_IN2) |
                          (1ULL << RR_IN1) | (1ULL << RR_IN2);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = pin_mask,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);
}

// Función general para fijar el estado de cada rueda: (fwd, rev) por rueda
void set_all_motors(int fl_f, int fl_r, int rl_f, int rl_r, 
                    int fr_f, int fr_r, int rr_f, int rr_r) {
    gpio_set_level(FL_IN1, fl_f); gpio_set_level(FL_IN2, fl_r);
    gpio_set_level(RL_IN1, rl_f); gpio_set_level(RL_IN2, rl_r);
    gpio_set_level(FR_IN1, fr_f); gpio_set_level(FR_IN2, fr_r);
    gpio_set_level(RR_IN1, rr_f); gpio_set_level(RR_IN2, rr_r);
}

// Movimientos Básicos Omnidireccionales
void robot_stop()       { set_all_motors(0,0, 0,0, 0,0, 0,0); }
void robot_forward()    { set_all_motors(1,0, 1,0, 1,0, 1,0); }
void robot_backward()   { set_all_motors(0,1, 0,1, 0,1, 0,1); }
void robot_strafe_left(){ set_all_motors(0,1, 1,0, 1,0, 0,1); }  
void robot_strafe_right(){ set_all_motors(1,0, 0,1, 0,1, 1,0); } 
void robot_rotate_left() { set_all_motors(0,1, 0,1, 1,0, 1,0); } 
void robot_rotate_right(){ set_all_motors(1,0, 1,0, 0,1, 0,1); }

// Interfaz Web Mejorada con botones de desplazamiento lateral (Strafe)
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char* resp_str = 
        "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<style>"
        "body { font-family: sans-serif; text-align: center; background: #1a1a1a; color: #fff; margin-top: 30px; }"
        "button { font-size: 18px; padding: 15px; margin: 5px; cursor: pointer; background: #007ACC; color: white; border: none; border-radius: 8px; width: 95px; }"
        "button:active { background: #005999; }"
        ".grid { display: grid; grid-template-columns: repeat(3, 110px); justify-content: center; gap: 8px; margin-top: 20px; }"
        "</style></head><body>"
        "<h2>ESP32 Omni 4WD Robot</h2>"
        "<div class=\"grid\">"
        "<button onclick=\"cmd('strafe_left')\">STRAFE L</button>"
        "<button onclick=\"cmd('forward')\">FWD</button>"
        "<button onclick=\"cmd('strafe_right')\">STRAFE R</button>"
        "<button onclick=\"cmd('rotate_left')\">ROT L</button>"
        "<button onclick=\"cmd('stop')\">STOP</button>"
        "<button onclick=\"cmd('rotate_right')\">ROT R</button>"
        "<div></div>"
        "<button onclick=\"cmd('backward')\">REV</button>"
        "<div></div>"
        "</div>"
        "<script>"
        "function cmd(action) { fetch('/' + action); }"
        "</script></body></html>";
    
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_action(httpd_req_t *req, void (*action)()) {
    action();
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t fwd_h(httpd_req_t *req)   { return handle_action(req, robot_forward); }
static esp_err_t rev_h(httpd_req_t *req)   { return handle_action(req, robot_backward); }
static esp_err_t stl_h(httpd_req_t *req)   { return handle_action(req, robot_strafe_left); }
static esp_err_t str_h(httpd_req_t *req)   { return handle_action(req, robot_strafe_right); }
static esp_err_t rtl_h(httpd_req_t *req)   { return handle_action(req, robot_rotate_left); }
static esp_err_t rtr_h(httpd_req_t *req)   { return handle_action(req, robot_rotate_right); }
static esp_err_t stp_h(httpd_req_t *req)   { return handle_action(req, robot_stop); }

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t r[] = {
            { "/", HTTP_GET, root_get_handler, NULL },
            { "/forward", HTTP_GET, fwd_h, NULL },
            { "/backward", HTTP_GET, rev_h, NULL },
            { "/strafe_left", HTTP_GET, stl_h, NULL },
            { "/strafe_right", HTTP_GET, str_h, NULL },
            { "/rotate_left", HTTP_GET, rtl_h, NULL },
            { "/rotate_right", HTTP_GET, rtr_h, NULL },
            { "/stop", HTTP_GET, stp_h, NULL },
        };
        for (int i = 0; i < 8; i++) httpd_register_uri_handler(server, &r[i]);
    }
    return server;
}

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    motors_init();
    robot_stop();

    wifi_init_softap();
    start_webserver();
}