#pragma once
#include <cstdint>
#include "driver/gpio.h"
#include "esp_rom_sys.h"

void init_onewire_sensors();

class OneWire {
public:
    explicit OneWire(gpio_num_t pin);

    bool reset();
    void write_bit(int bit);
    int  read_bit();
    void write_byte(uint8_t byte);
    uint8_t read_byte();
    

private:
    gpio_num_t pin_;
    inline void delay_us(uint32_t us) { esp_rom_delay_us(us); }
};
