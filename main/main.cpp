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

static const char *TAG = "ldx";

static SemaphoreHandle_t wifiSemaphore;

#include "apds9960.h"


#define APDS9960_VL_IO                       GPIO_NUM_18
#define APDS9960_I2C_MASTER_SCL_IO           GPIO_NUM_20             /*!< gpio number for I2C master clock */
#define APDS9960_I2C_MASTER_SDA_IO           GPIO_NUM_19             /*!< gpio number for I2C master data  */

#define APDS9960_I2C_MASTER_NUM              I2C_NUM_0              /*!< I2C port number for master dev */
#define APDS9960_I2C_MASTER_TX_BUF_DISABLE   0                      /*!< I2C master do not need buffer */
#define APDS9960_I2C_MASTER_RX_BUF_DISABLE   0                      /*!< I2C master do not need buffer */
#define APDS9960_I2C_MASTER_FREQ_HZ          100000                 /*!< I2C master clock frequency */

i2c_bus_handle_t i2c_bus = NULL;
apds9960_handle_t apds9960 = NULL;

void apds9960_gpio_vl_init()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = BIT64(APDS9960_VL_IO);  // Use BIT64 if gpio_num_t > 31
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(APDS9960_VL_IO, 0);
}



void apds9960_test_init()
{
    auto i2c_master_port = APDS9960_I2C_MASTER_NUM;

    i2c_config_t conf = {};  // Zero-initialize to avoid garbage in unused fields
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = APDS9960_I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = APDS9960_I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = APDS9960_I2C_MASTER_FREQ_HZ;
    i2c_bus = i2c_bus_create(i2c_master_port, &conf);
    apds9960 = apds9960_create(i2c_bus, APDS9960_I2C_ADDRESS);
}






static void localEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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
        return; // Exit if unable to sync
    }
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    ESP_LOGI("TimeTest", "Current local time and date: %d-%d-%d %02d:%02d:%02d",
             1900 + timeinfo.tm_year, 1 + timeinfo.tm_mon, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}


void button_task(void *pvParameters) {
	WiFiManager *wifiManager = static_cast<WiFiManager*>(pvParameters);  // Cast the void pointer back to WiFiManager pointer

    Button button(static_cast<gpio_num_t>(CONFIG_BUTTON_PIN));
    while (1) {
        if (button.longPressed()) {
            ESP_LOGI("BUTTON", "Long press detected, resetting WiFi settings.");
            wifiManager->clear();
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100 ms
    }
}


typedef struct {
    void* i2c_dev;
    // We only need the first member
} apds9960_dev_stub_t;



extern "C" void app_main() {
	NvsStorageManager nv;
	SettingsManager settings(nv);

    apds9960_gpio_vl_init();
    apds9960_test_init();
    apds9960_gesture_init(apds9960);
    gpio_set_level(APDS9960_VL_IO, 0);
	
    wifiSemaphore = xSemaphoreCreateBinary();
	WiFiManager wifiManager(nv, localEventHandler, nullptr);
	xTaskCreate(button_task, "button_task", 2048, &wifiManager, 10, NULL);
    if (xSemaphoreTake(wifiSemaphore, portMAX_DELAY) ) {
		ESP_LOGI(TAG, "Main task continues after WiFi connection.");
		initialize_sntp(settings);

		static WebServer::WebContext ctx{settings, wifiManager};
        static WebServer webServer{ctx};

        if (webServer.start() == ESP_OK) {
            ESP_LOGI(TAG, "Web server started successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to start web server.");
        }

        extern uint8_t apds9960_get_enable(apds9960_handle_t sensor);

        uint8_t enable = apds9960_get_enable(apds9960);
        ESP_LOGI("APDS9960", "ENABLE register: 0x%02X", enable);

#if 1            
#define APDS9960_ENABLE 0x80
        i2c_bus_write_byte(
            reinterpret_cast<apds9960_dev_stub_t*>(apds9960)->i2c_dev,
            APDS9960_ENABLE,
            0b00000011  // PON | AEN
        );
#endif

		while (true) {

            uint16_t r = 0, g = 0, b = 0, c = 0;


            apds9960_get_color_data(apds9960, &r, &g, &b, &c);
            ESP_LOGI("APDS9960", "R:%u G:%u B:%u C:%u", r, g, b, c);
#if 0
            if (apds9960_color_data_ready(apds9960)) {
                if (apds9960_get_color_data(apds9960, &r, &g, &b, &c) == ESP_OK) {
                    float lux = apds9960_calculate_lux(apds9960, r, g, b);
                    uint16_t cct = apds9960_calculate_color_temperature(apds9960, r, g, b);
                    ESP_LOGI("APDS9960", "Lux: %.2f, Color Temp: %uK", lux, cct);
                } else {
                    ESP_LOGW("APDS9960", "Failed to read color data");
                }
            } else {
                ESP_LOGI(TAG, "no data");
            }
#endif
			vTaskDelay(pdMS_TO_TICKS(1000)); 
		
		}
	}

}
