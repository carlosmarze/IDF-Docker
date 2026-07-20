#include "pr_onewire.h"
#include "pr_onewiresearch.h"
#include "config_proyecto.h"

#include "driver/gpio.h"
#include "utils_logger.h"
#include <vector>

#define TAG "ONEWIRE"

OneWire::OneWire(gpio_num_t pin) : pin_(pin) {
    gpio_set_direction(pin_, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(pin_, GPIO_PULLUP_ONLY);
    LOGI(TAG, "Inicializando OneWire en pin %d", pin_);
}

bool OneWire::reset() {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
    delay_us(480);

    gpio_set_direction(pin_, GPIO_MODE_INPUT);
    delay_us(70);

    bool presence = (gpio_get_level(pin_) == 0);
    delay_us(410);
    return presence;
}

void OneWire::write_bit(int bit) {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
    delay_us(bit ? 10 : 60);

    gpio_set_direction(pin_, GPIO_MODE_INPUT);
    delay_us(bit ? 55 : 5);
}

int OneWire::read_bit() {
    gpio_set_direction(pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(pin_, 0);
    delay_us(3);

    gpio_set_direction(pin_, GPIO_MODE_INPUT);
    delay_us(10);

    int bit = gpio_get_level(pin_);
    delay_us(50);
    return bit;
}

void OneWire::write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++)
        write_bit((byte >> i) & 1);
}

uint8_t OneWire::read_byte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
        byte |= (read_bit() << i);
    return byte;
}

//Inicialización de sensores
//static std::vector<std::array<uint8_t, 8>> g_sensors;
//static int g_onewire_pin = GPIO_NUM_4;

void init_onewire_sensors()
{
    OneWire ow((gpio_num_t)g_onewire_pin);
    OneWireSearch search(ow);

    uint8_t rom[8];
    g_sensors.clear();

    while (search.next(rom)) {
        std::array<uint8_t, 8> r;
        memcpy(r.data(), rom, 8);
        g_sensors.push_back(r);
    }

    LOGI(TAG, "Sensores detectados: %d", g_sensors.size());
    onewire_init = true; // Indica que la inicialización se ha completado
}
