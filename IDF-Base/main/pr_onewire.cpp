#include "pr_onewire.h"
//#include "pr_onewiresearch.h"
#include "config_proyecto.h"

#include "driver/gpio.h"
#include "utils_logger.h"
#include <vector>

#define TAG "ONEWIRE"

QueueHandle_t q_onewire = nullptr;
int g_onewire_pin = -1;   // sin configurar
std::vector<std::array<uint8_t, 8>> g_sensors;
bool onewire_init = false; // Variable global para indicar si el escaneo de OneWire se ha completado
std::string tempjson; //json con la lectura de la temperatura

static bool onewire_idle = true; //para leer el resultado
static bool first_init = true; //para inicializar la cola y la tarea de onewire solo una vez

bool is_onewire_idle() {
    return onewire_idle;
}
void set_onewire_idle(bool stat) {
    onewire_idle = stat;
}

OneWire::OneWire(gpio_num_t pin) : pin_(pin) {   //init, se corre al crear la clase
    gpio_set_direction(pin_, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_pull_mode(pin_, GPIO_PULLUP_ONLY);
    if(first_init) {  //esto porque siempre entra acá cada vez que se crea la clase para una lectura
        LOGI(TAG, "Creando OneWire en pin %d", pin_);
        first_init = false;
    }
    
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
    if (g_onewire_pin == -1) {
        LOGW(TAG, "Pin para Init no configurado.");
        return;
    }

    OneWire ow((gpio_num_t)g_onewire_pin);
    OneWireSearch search(ow);

    uint8_t rom[8];
    g_sensors.clear();

    LOGI(TAG, "Buscando sensores en pin %d...", g_onewire_pin);

    while (search.next(rom)) {

        // Copiar ROM al vector global
        std::array<uint8_t, 8> r;
        memcpy(r.data(), rom, 8);
        g_sensors.push_back(r);

        // Convertir ROM a string para log
        char rom_str[17];
        snprintf(rom_str, sizeof(rom_str),
            "%02X%02X%02X%02X%02X%02X%02X%02X",
            rom[0], rom[1], rom[2], rom[3],
            rom[4], rom[5], rom[6], rom[7]);

        LOGI(TAG, "Sensor detectado: %s", rom_str);
    }

    LOGI(TAG, "Total Sensores: %d", g_sensors.size());
    onewire_init = true;
}

//Tareas de onewire
//int g_onewire_pin = ONEWIRE_DEFAULT_PIN; // Valor por defecto para el pin OneWire

void task_onewire(void *p)
{
    OneWireCommand cmd;

    while (true) {

        if (xQueueReceive(q_onewire, &cmd, portMAX_DELAY)) {

            switch (cmd.type) {

                case OneWireCmdType::RESCAN:{
                    init_onewire_sensors();
                    break;
                }
                case OneWireCmdType::READ_ALL:
                case OneWireCmdType::READ_FAST:
                case OneWireCmdType::READ_RAW:
                case OneWireCmdType::READ_ONE:
                    cmd_onewire(cmd);
                    break;
            }
        }
    }
}

//Antes en comando
void cmd_onewire(const OneWireCommand &cmd)
{
    onewire_idle = false; //marco que estoy procesando el comando
    if (g_onewire_pin < 0) {
        LOGE(TAG, "Pin no configurado");
        return;
    }

    OneWire ow((gpio_num_t)g_onewire_pin);

    tempjson = "{\"sensors\":[";

    for (auto &rom : g_sensors) {

        char rom_str[17];
        snprintf(rom_str, sizeof(rom_str),
            "%02X%02X%02X%02X%02X%02X%02X%02X",
            rom[0], rom[1], rom[2], rom[3],
            rom[4], rom[5], rom[6], rom[7]);

        if (cmd.type == OneWireCmdType::READ_ONE && cmd.id != rom_str)
            continue;

        DS18B20 sensor(ow, rom.data());

        float temp = 0;
        uint8_t scratch[9];

        switch (cmd.type) {
            case OneWireCmdType::READ_FAST:
                temp = sensor.read_temperature_fast();
                break;

            case OneWireCmdType::READ_RAW:
                temp = sensor.read_temperature();
                sensor.read_scratchpad(scratch);
                break;

            default:
                temp = sensor.read_temperature();
                break;
        }

        char entry[256];

        if (cmd.type != OneWireCmdType::READ_RAW) {
            snprintf(entry, sizeof(entry),
                "{\"id\":\"%s\",\"temp\":%.2f},",
                rom_str, temp);
        } else {
            char rawbuf[64];
            char *p = rawbuf;
            for (int i = 0; i < 9; i++)
                p += sprintf(p, "%02X", scratch[i]);

            snprintf(entry, sizeof(entry),
                "{\"id\":\"%s\",\"temp\":%.2f,\"raw\":\"%s\"},",
                rom_str, temp, rawbuf);
        }

        tempjson += entry;
    }

    if (!tempjson.empty() && tempjson.back() == ',')
        tempjson.pop_back();

    tempjson += "]}";
    onewire_idle = true; //marco que terminé de procesar el comando
    LOGI(TAG, "Lectura: %s", tempjson.c_str());
}


//Antes en onedrivesearch
OneWireSearch::OneWireSearch(OneWire &ow) : ow_(ow) {
    reset_search();
}

void OneWireSearch::reset_search() {
    last_discrepancy_ = 0;
    last_device_ = false;
    memset(rom_, 0, sizeof(rom_));
}

bool OneWireSearch::next(uint8_t out_rom[8]) {
    if (last_device_) return false;

    if (!ow_.reset()) return false;

    ow_.write_byte(0xF0); // SEARCH ROM

    uint8_t new_discrepancy = 0;

    for (int bit = 0; bit < 64; bit++) {
        int b = ow_.read_bit();
        int c = ow_.read_bit();

        int chosen;

        if (b == 1 && c == 1)
            return false;

        if (b == 0 && c == 0) {
            if (bit == last_discrepancy_)
                chosen = 1;
            else if (bit > last_discrepancy_)
                chosen = 0;
            else
                chosen = (rom_[bit / 8] >> (bit % 8)) & 1;

            if (chosen == 0)
                new_discrepancy = bit;
        } else {
            chosen = b;
        }

        if (chosen)
            rom_[bit / 8] |= (1 << (bit % 8));
        else
            rom_[bit / 8] &= ~(1 << (bit % 8));

        ow_.write_bit(chosen);
    }

    last_discrepancy_ = new_discrepancy;
    if (last_discrepancy_ == 0)
        last_device_ = true;

    memcpy(out_rom, rom_, 8);
    return true;
}

//antes en ds18b20
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