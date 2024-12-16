#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mcp9808.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_TCP";
static esp_mqtt_client_handle_t mqtt_client = NULL;

// I2C configuration
#define I2C_MASTER_SCL_IO           22    // GPIO22 for SCL
#define I2C_MASTER_SDA_IO           21    // GPIO21 for SDA
#define I2C_MASTER_NUM              I2C_NUM_1
#define I2C_MASTER_FREQ_HZ          100000  // I2C speed (100kHz)
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

esp_err_t i2c_master_init() {
    ESP_LOGI(TAG, "Initializing I2C master...");

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,                // Set to master mode
        .sda_io_num = I2C_MASTER_SDA_IO,        // SDA pin
        .scl_io_num = I2C_MASTER_SCL_IO,        // SCL pin
        .sda_pullup_en = GPIO_PULLUP_ENABLE,    // Enable pull-up resistors
        .scl_pullup_en = GPIO_PULLUP_ENABLE,    // Enable pull-up resistors
        .master.clk_speed = I2C_MASTER_FREQ_HZ, // I2C clock speed
    };

    // Initialize the I2C driver
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Install the I2C driver
    err = i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C driver installed successfully.");
    return ESP_OK;
}

// Function to handle WiFi events
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi connecting...");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected.");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi lost connection.");
            break;
        case IP_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "WiFi got IP.");
            break;
        default:
            break;
    }
}

// WiFi connection setup
void wifi_connection() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_initiation));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = "NCAIR IOT",
            .password = "Asim@123Tewari",
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// MQTT event callback handler
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
    mqtt_client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(mqtt_client, "/nodejs/mqtt", 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        default:
            ESP_LOGI(TAG, "Unhandled MQTT event: %d", event->event_id);
            break;
    }
    return ESP_OK;
}

// Wrapper for the MQTT event handler
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data) {
    mqtt_event_handler_cb(data);
}

// MQTT client initialization and startup
void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://broker.emqx.io",
        .port = 1883,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize WiFi
    wifi_connection();
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "WiFi was initiated.");

    // Initialize MQTT
    mqtt_app_start();
    ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return;
    }
     // MCP9808 configuration
    mcp9808_config_t mcp_config;
    mcp_config.address = 0x18; // Default I2C address for MCP9808
    mcp_config.i2c_num = I2C_NUM_1; // Use I2C_NUM_1 for communication

    mcp9808_handle_t mcp;
    uint16_t mcp_manuf_id;
    uint16_t mcp_device_id;

    // Initialize MCP9808 sensor
    if (mcp9808_init(&mcp_config, &mcp, &mcp_manuf_id, &mcp_device_id) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize MCP9808");
        return;
    }

    // Periodic publishing in main loop
    while (1) {
        if (mqtt_client != NULL) {
             float temperature;
        if (mcp9808_ambient_temp(mcp, &temperature) == ESP_OK) {
            ESP_LOGI(TAG, "Temperature: %0.2f °C", temperature);
             char str[20];
            snprintf(str, sizeof(str), "%.2f", temperature);
            esp_mqtt_client_publish(mqtt_client, "/nodejs/mqtt",str, 0, 1, 0);
            ESP_LOGI(TAG, "Message published to /nodejs/mqtt");
        } else {
            // Log the error but continue
            ESP_LOGE(TAG, "Failed to read temperature from MCP9808");
            esp_mqtt_client_publish(mqtt_client, "/nodejs/mqtt", "Failed to read temperature from MCP9808", 0, 1, 0);
            ESP_LOGI(TAG, "Message published to /nodejs/mqtt");
        }
      }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay of 2 seconds
    }
}
