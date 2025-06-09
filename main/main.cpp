#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "Button.h"
#include "WifiManager.h"
#include "SettingsManager.h"
#include "WebServer.h"
#include "apds9960_sensor.h"

static const char *TAG = "ldx";
static SemaphoreHandle_t wifiSemaphore;

void localEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        xSemaphoreGive(wifiSemaphore);
    }
}

void initialize_sntp(SettingsManager& settings) {
    setenv("TZ", settings.tz.c_str(), 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, settings.ntpServer.c_str());
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP service initialized");

    int max_retry = 200;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && max_retry--) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    if (max_retry <= 0) {
        ESP_LOGE(TAG, "Failed to synchronize NTP time");
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI("TimeTest", "Current local time and date: %d-%d-%d %02d:%02d:%02d",
             1900 + timeinfo.tm_year, 1 + timeinfo.tm_mon, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void button_task(void *pvParameters) {
    WiFiManager *wifiManager = static_cast<WiFiManager*>(pvParameters);
    Button button(static_cast<gpio_num_t>(CONFIG_BUTTON_PIN));
    while (1) {
        if (button.longPressed()) {
            ESP_LOGI("BUTTON", "Long press detected, resetting WiFi settings.");
            wifiManager->clear();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

extern "C" void app_main() {
    NvsStorageManager nv;
    SettingsManager settings(nv);

    Apds9960ColorSensor sensor;
    if (!sensor.init()) {
        ESP_LOGE(TAG, "Failed to initialize APDS9960 sensor");
    }

    wifiSemaphore = xSemaphoreCreateBinary();
    WiFiManager wifiManager(nv, localEventHandler, nullptr);
    xTaskCreate(button_task, "button_task", 2048, &wifiManager, 10, NULL);

    if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY)) {
        ESP_LOGI(TAG, "Main task continues after WiFi connection.");
        initialize_sntp(settings);

        static WebServer::WebContext ctx{settings, wifiManager};
        static WebServer webServer{ctx};

        if (webServer.start() == ESP_OK) {
            ESP_LOGI(TAG, "Web server started successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to start web server.");
        }

        while (true) {
            ColorReading reading;
            if (sensor.read(reading)) {
                ESP_LOGI("APDS9960", "R:%u G:%u B:%u C:%u Lux: %.2f CCT: %uK",
                         reading.red, reading.green, reading.blue, reading.clear,
                         reading.lux, reading.colorTemperature);
            } else {
                ESP_LOGW("APDS9960", "Failed to read color data");
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

