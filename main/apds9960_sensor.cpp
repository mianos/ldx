
#include "apds9960_sensor.h"
#include "apds9960.h"
#include "i2c_bus.h"
#include "driver/gpio.h"

Apds9960ColorSensor::Apds9960ColorSensor(int i2c_sda_gpio, int i2c_scl_gpio, i2c_port_t i2c_port, int freq_hz, int vl_gpio)
    : sda_gpio_(i2c_sda_gpio),
      scl_gpio_(i2c_scl_gpio),
      i2c_port_(i2c_port),
      freq_hz_(freq_hz),
      vl_gpio_(vl_gpio) {}

Apds9960ColorSensor::~Apds9960ColorSensor() {
    if (apds9960_) {
        apds9960_delete(reinterpret_cast<apds9960_handle_t*>(&apds9960_));
    }
    if (i2c_bus_) {
        i2c_bus_delete(reinterpret_cast<i2c_bus_handle_t*>(&i2c_bus_));
    }
}

bool Apds9960ColorSensor::init() {
    gpio_config_t gpio_cfg = {};
    gpio_cfg.pin_bit_mask = 1ULL << vl_gpio_;
    gpio_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&gpio_cfg);
    gpio_set_level(static_cast<gpio_num_t>(vl_gpio_), 0);

    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = static_cast<gpio_num_t>(sda_gpio_);
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = static_cast<gpio_num_t>(scl_gpio_);
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = freq_hz_;

    i2c_bus_ = i2c_bus_create(i2c_port_, &conf);
    if (!i2c_bus_) return false;

    apds9960_ = apds9960_create(reinterpret_cast<i2c_bus_handle_t>(i2c_bus_), APDS9960_I2C_ADDRESS);
    if (!apds9960_) return false;

    apds9960_gesture_init(reinterpret_cast<apds9960_handle_t>(apds9960_));
    apds9960_set_adc_integration_time(reinterpret_cast<apds9960_handle_t>(apds9960_), 100);
    apds9960_enable_color_engine(reinterpret_cast<apds9960_handle_t>(apds9960_), true);

    return true;
}

bool Apds9960ColorSensor::read(ColorReading& out) {
    uint16_t r = 0, g = 0, b = 0, c = 0;

    if (apds9960_get_color_data(reinterpret_cast<apds9960_handle_t>(apds9960_), &r, &g, &b, &c) != ESP_OK) {
        return false;
    }

    out.red = r;
    out.green = g;
    out.blue = b;
    out.clear = c;
    out.lux = apds9960_calculate_lux(reinterpret_cast<apds9960_handle_t>(apds9960_), r, g, b);
    out.colorTemperature = apds9960_calculate_color_temperature(reinterpret_cast<apds9960_handle_t>(apds9960_), r, g, b);
    return true;
}

