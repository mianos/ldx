
#pragma once

#include <cstdint>
#include "i2c_bus.h"

struct ColorReading {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint16_t clear;
    float lux;
    uint16_t colorTemperature;
};

class Apds9960ColorSensor {
public:
    Apds9960ColorSensor(int i2c_sda_gpio = 19,
                        int i2c_scl_gpio = 20,
                        i2c_port_t i2c_port = I2C_NUM_0,
                        int freq_hz = 100000,
                        int vl_gpio = 18);
    ~Apds9960ColorSensor();

    bool init();
    bool read(ColorReading& out);

private:
    int sda_gpio_;
    int scl_gpio_;
    i2c_port_t i2c_port_;
    int freq_hz_;
    int vl_gpio_;
    void* i2c_bus_ = nullptr;
    void* apds9960_ = nullptr;
};

