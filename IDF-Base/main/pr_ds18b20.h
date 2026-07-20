#pragma once
#include <cstdint>
#include <cmath>
#include "pr_onewire.h"

class DS18B20 {
public:
    DS18B20(OneWire &ow, const uint8_t rom[8]);

    float read_temperature();
    const uint8_t* rom() const { return rom_; }
    float read_temperature_fast();
    void read_scratchpad(uint8_t data[9]);


private:
    OneWire &ow_;
    uint8_t rom_[8];

    void match_rom();
};
