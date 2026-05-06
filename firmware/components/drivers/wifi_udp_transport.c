#include "wifi_udp_transport.h"

#include <errno.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define WIFI_READY_BIT BIT0
#define WIFI_FAIL_BIT  BIT1

static const char *TAG = "wifi_udp";
static EventGroupHandle_t s_wifi_events;
static wifi_udp_transport_config_t s_config;
static bool s_initialized;
static bool s_wifi_ready;
static int s_socket = -1;
static struct sockaddr_in s_ground_addr;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_ready = false;
        if (s_wifi_events != NULL) {
            xEventGroupClearBits(s_wifi_events, WIFI_READY_BIT);
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        (void)esp_wifi_connect();
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_ready = true;
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_READY_BIT);
        }
        ESP_LOGI(TAG, "Wi-Fi UDP backup online");
    }
}

static esp_err_t ensure_udp_socket(void) {
    if (s_socket >= 0) {
        return ESP_OK;
    }

    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_socket < 0) {
        ESP_LOGE(TAG, "UDP socket failed errno=%d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_config.local_port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(s_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "UDP bind port=%u failed errno=%d", (unsigned)s_config.local_port, errno);
        close(s_socket);
        s_socket = -1;
        return ESP_FAIL;
    }

    memset(&s_ground_addr, 0, sizeof(s_ground_addr));
    s_ground_addr.sin_family = AF_INET;
    s_ground_addr.sin_port = htons(s_config.ground_port);
    if (inet_pton(AF_INET, s_config.ground_host, &s_ground_addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "Ground host must be IPv4 dotted decimal: %s", s_config.ground_host);
        close(s_socket);
        s_socket = -1;
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(
        TAG,
        "UDP backup bound local=%u ground=%s:%u",
        (unsigned)s_config.local_port,
        s_config.ground_host,
        (unsigned)s_config.ground_port
    );
    return ESP_OK;
}

esp_err_t wifi_udp_transport_init(const wifi_udp_transport_config_t *config) {
    if (config == NULL || config->ssid == NULL || config->ground_host == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->ssid[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi UDP backup disabled; SSID is empty");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_initialized) {
        return ESP_OK;
    }

    s_config = *config;
    if (s_config.connect_timeout_ms == 0) {
        s_config.connect_timeout_ms = 10000;
    }

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result != ESP_OK && nvs_result != ESP_ERR_NVS_NO_FREE_PAGES && nvs_result != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_result, TAG, "nvs init failed");
    }

    esp_err_t netif_result = esp_netif_init();
    if (netif_result != ESP_OK && netif_result != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(netif_result, TAG, "netif init failed");
    }

    esp_err_t loop_result = esp_event_loop_create_default();
    if (loop_result != ESP_OK && loop_result != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(loop_result, TAG, "event loop init failed");
    }

    (void)esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init failed");

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
        TAG,
        "wifi event handler failed"
    );
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
        TAG,
        "ip event handler failed"
    );

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, s_config.ssid, sizeof(wifi_config.sta.ssid));
    if (s_config.password != NULL) {
        strlcpy((char *)wifi_config.sta.password, s_config.password, sizeof(wifi_config.sta.password));
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if (wifi_config.sta.password[0] == '\0') {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "wifi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_READY_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(s_config.connect_timeout_ms)
    );
    if ((bits & WIFI_READY_BIT) == 0) {
        ESP_LOGW(TAG, "Wi-Fi connect timed out for SSID=%s", s_config.ssid);
        s_initialized = true;
        return ESP_ERR_TIMEOUT;
    }

    ESP_RETURN_ON_ERROR(ensure_udp_socket(), TAG, "udp socket failed");
    s_initialized = true;
    return ESP_OK;
}

bool wifi_udp_transport_is_ready(void) {
    return s_initialized && s_wifi_ready && s_socket >= 0;
}

esp_err_t wifi_udp_transport_send(const uint8_t *payload, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (payload == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_udp_transport_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    int sent = sendto(s_socket, payload, len, 0, (struct sockaddr *)&s_ground_addr, sizeof(s_ground_addr));
    if (sent < 0 || (size_t)sent != len) {
        ESP_LOGW(TAG, "UDP send failed errno=%d sent=%d len=%u", errno, sent, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_udp_transport_receive(uint8_t *payload, size_t capacity, size_t *out_len, uint32_t timeout_ms) {
    if (payload == NULL || out_len == NULL || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = 0;
    if (!wifi_udp_transport_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(s_socket, &readfds);
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    int ready = select(s_socket + 1, &readfds, NULL, NULL, &timeout);
    if (ready == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (ready < 0) {
        return ESP_FAIL;
    }

    int received = recvfrom(s_socket, payload, capacity, 0, NULL, NULL);
    if (received < 0) {
        return ESP_FAIL;
    }
    if (received == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    *out_len = (size_t)received;
    return ESP_OK;
}
