#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


#define ONEWIRE_DEFAULT_PIN GPIO_NUM_4 // Valor por defecto para el pin OneWire

extern int g_onewire_pin; //pin donde están conectados los sensores DS18B20. Se puede cambiar con el comando "temp_scan pin=<gpio>".
extern std::vector<std::array<uint8_t, 8>> g_sensors;
extern bool onewire_init; // Variable global para indicar si el escaneo de OneWire se ha completado
extern std::string tempjson; //json con la lectura de la temperatura
enum class OneWireCmdType {
    RESCAN,
    READ_ALL,
    READ_FAST,
    READ_RAW,
    READ_ONE
};

struct OneWireCommand {
    OneWireCmdType type;
    std::string id;   // opcional
};

extern QueueHandle_t q_onewire;



void init_onewire_sensors();
void task_onewire(void *p);
void cmd_onewire(const OneWireCommand &cmd);
bool is_onewire_idle();

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

//antes en onedrivesearch.h
class OneWireSearch {
public:
    explicit OneWireSearch(OneWire &ow);

    void reset_search();
    bool next(uint8_t out_rom[8]);
    

private:
    OneWire &ow_;
    uint8_t rom_[8];
    uint8_t last_discrepancy_;
    bool last_device_;
};

//antes en ds18b20
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