#include "pr_ds18b20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

DS18B20::DS18B20(OneWire &ow, const uint8_t rom[8]) : ow_(ow) {
    memcpy(rom_, rom, 8);
}

void DS18B20::match_rom() {
    ow_.write_byte(0x55);
    for (int i = 0; i < 8; i++)
        ow_.write_byte(rom_[i]);
}

float DS18B20::read_temperature() {
    if (!ow_.reset()) return NAN;

    match_rom();
    ow_.write_byte(0x44); // CONVERT T
    vTaskDelay(pdMS_TO_TICKS(750));

    if (!ow_.reset()) return NAN;

    match_rom();
    ow_.write_byte(0xBE); // READ SCRATCHPAD

    uint8_t data[9];
    for (int i = 0; i < 9; i++)
        data[i] = ow_.read_byte();

    int16_t raw = (data[1] << 8) | data[0];
    return raw / 16.0f;
}

float DS18B20::read_temperature_fast() {
    if (!ow_.reset()) return NAN;

    match_rom();
    ow_.write_byte(0x44); // CONVERT T (pero no esperamos)
    vTaskDelay(pdMS_TO_TICKS(50)); // lectura rápida

    if (!ow_.reset()) return NAN;

    match_rom();
    ow_.write_byte(0xBE);

    uint8_t data[9];
    for (int i = 0; i < 9; i++)
        data[i] = ow_.read_byte();

    int16_t raw = (data[1] << 8) | data[0];
    return raw / 16.0f;
}

void DS18B20::read_scratchpad(uint8_t data[9]) {
    if (!ow_.reset()) return;

    match_rom();
    ow_.write_byte(0xBE);

    for (int i = 0; i < 9; i++)
        data[i] = ow_.read_byte();
}

