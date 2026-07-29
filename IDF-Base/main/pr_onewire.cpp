#include "pr_onewire.h"
#include "config_proyecto.h"
#include "utils_logger.h"

#include "driver/gpio.h"
#include <vector>
#include "esp_log.h"
#include <unordered_map> //para reading_changed
#include <cmath> //para reading_changed

#define TAG "ONEWIRE"

QueueHandle_t q_onewire = nullptr;
int g_onewire_pin = -1;   // sin configurar
std::vector<std::array<uint8_t, 8>> g_sensors;
bool onewire_init = false; // Variable global para indicar si el escaneo de OneWire se ha completado
std::string tempjson; //json con la lectura de la temperatura

static bool onewire_idle = true; //para leer el resultado
static bool first_init = true; //para inicializar la cola y la tarea de onewire solo una vez
static std::vector<SensorReading> last_readings;

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

    for(int i = 0; i < 2; i++) {
        ow.reset();
        vTaskDelay(pdMS_TO_TICKS(10));   // estabilizar el bus

        OneWireSearch search(ow);

        uint8_t rom[8];
        g_sensors.clear();

        LOGN(TAG, "Buscando sensores en pin %d...", g_onewire_pin);

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

            LOGN(TAG, "Sensor detectado: %s", rom_str);
        }
        
        if(g_sensors.empty()) {
            LOGW(TAG, "No se encontraron sensores. Reintento %d.", i + 1);
        } else {
            break; // Salir del bucle después de un intento exitoso
        }
    }

    LOGN(TAG, "Total Sensores: %d", g_sensors.size());
    onewire_init = true;
}

//Tareas de onewire
//int g_onewire_pin = ONEWIRE_DEFAULT_PIN; // Valor por defecto para el pin OneWire

void task_onewire(void *p)
{
    OneWireCommand cmd;

    while (true) {

        if (xQueueReceive(q_onewire, &cmd, portMAX_DELAY)) {
            init_onewire_sensors(); //lo ponemos porque si no, cada tanto, se cuelga la lectura
            switch (cmd.type) {

                case OneWireCmdType::RESCAN:{
                    //init_onewire_sensors();
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


bool readings_changedOLD(const std::vector<SensorReading>& oldr,
                      const std::vector<SensorReading>& newr)
{
    if (oldr.size() != newr.size())
        return true;

    for (size_t i = 0; i < newr.size(); i++) {
        if (oldr[i].id != newr[i].id)
            return true;

        if (fabs(oldr[i].temp - newr[i].temp) > TEMPTOLERANCIA)   // tolerancia mínima
            return true;
    }

    return false;
}


//Antes en comando


bool readings_changed(const std::vector<SensorReading>& oldr,
                      const std::vector<SensorReading>& newr)
{
    // Si distinto número de sensores, considerar cambio
    if (oldr.size() != newr.size())
        return true;

    std::unordered_map<std::string, float> oldmap;
    oldmap.reserve(oldr.size());
    for (const auto &s : oldr) oldmap[s.id] = s.temp;

    for (const auto &s : newr) {
        auto it = oldmap.find(s.id);
        if (it == oldmap.end()) {
            // sensor nuevo o ID distinto
            return true;
        }
        if (std::fabs(it->second - s.temp) > TEMPTOLERANCIA) {
            return true;
        }
    }

    return false;
}

void cmd_onewire(const OneWireCommand &cmd)
{
    std::vector<SensorReading> new_readings;
    std::vector<std::string> entries;
    new_readings.reserve(g_sensors.size());
    entries.reserve(g_sensors.size());

    onewire_idle = false;
    if (g_onewire_pin < 0) {
        LOGE(TAG, "Pin no configurado");
        onewire_idle = true;
        return;
    }

    OneWire ow((gpio_num_t)g_onewire_pin);

    // 1) Leer sensores y construir new_readings + entries
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

        // Guardar lectura estructurada
        SensorReading sr;
        sr.id = rom_str;
        sr.temp = temp;
        new_readings.push_back(sr);

        // Construir entrada JSON (RAW incluido)
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

        entries.emplace_back(entry);
    }

    // 2) Control de falsas mediciones + sensores desaparecidos
    if (!last_readings.empty()) {

        // mapa id -> temp anterior
        std::unordered_map<std::string, float> lastmap;
        lastmap.reserve(last_readings.size());
        for (const auto &s : last_readings) lastmap[s.id] = s.temp;

        // A) corregir lecturas inválidas
        for (size_t i = 0; i < new_readings.size(); ++i) {
            auto it = lastmap.find(new_readings[i].id);
            if (it != lastmap.end()) {

                float last_temp = it->second;

                if (std::fabs(new_readings[i].temp - last_temp) > TEMPMAXDELTA) {

                    LOGW(TAG, "Lectura de sensor %s difiere mucho de la anterior (%.2f vs %.2f). Ignorando cambio.",
                         new_readings[i].id.c_str(),
                         new_readings[i].temp,
                         last_temp);

                    // restaurar lectura
                    new_readings[i].temp = last_temp;

                    // actualizar entrada JSON (incluye RAW si estaba)
                    char fixed_entry[256];
                    snprintf(fixed_entry, sizeof(fixed_entry),
                        "{\"id\":\"%s\",\"temp\":%.2f},",
                        new_readings[i].id.c_str(), new_readings[i].temp);

                    entries[i] = std::string(fixed_entry);
                }
            }
        }

        // B) sensores desaparecidos → agregarlos con valor anterior
        for (const auto &old : last_readings) {

            bool found = false;
            for (const auto &nr : new_readings) {
                if (nr.id == old.id) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                LOGW(TAG, "Sensor %s no detectado. Usando valor anterior %.2f.",
                     old.id.c_str(), old.temp);

                new_readings.push_back(old);

                char entry[256];
                snprintf(entry, sizeof(entry),
                        "{\"id\":\"%s\",\"temp\":%.2f},",
                        old.id.c_str(), old.temp);

                entries.push_back(std::string(entry));
            }
        }
    }

    // 3) Construir JSON final una sola vez
    std::string local_json = "{\"sensors\":[";
    for (const auto &e : entries) local_json += e;
    if (!local_json.empty() && local_json.back() == ',')
        local_json.pop_back();
    local_json += "]}";

    tempjson = local_json;

    // 4) Comparar lecturas
    bool changed = readings_changed(last_readings, new_readings);

    if (changed) {
        LOGI(TAG, "Lectura cambio: %s", tempjson.c_str());
    } else {
        LOGN(TAG, "Lectura igual, no se loguea detalle");
    }

    // 5) Actualizar última lectura
    last_readings = new_readings;

    onewire_idle = true;
}


void cmd_onewireOLD(const OneWireCommand &cmd)
{
    std::vector<SensorReading> new_readings; //nueva lectura
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
        //guardo la lectura en la estructura para poder compararla con la anteior y ver si hubo cambios
        SensorReading sr;
        sr.id = rom_str;
        sr.temp = temp;
        new_readings.push_back(sr);

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

    //Control de falsas mediciones. Si la temperatura difiere mucho de la anterior, no la guardamos como última lectura
    if (!last_readings.empty() && !new_readings.empty()) {
        for (size_t i = 0; i < new_readings.size(); i++) {
            for (size_t j = 0; j < last_readings.size(); j++) {
                if (new_readings[i].id == last_readings[j].id) {
                    if (fabs(new_readings[i].temp - last_readings[j].temp) > TEMPMAXDELTA) { //diferencia mayor a TEMPMAXDELTA
                        LOGW(TAG, "Lectura de sensor %s difiere mucho de la anterior (%.2f vs %.2f). Ignorando cambio.",
                            new_readings[i].id.c_str(),
                            new_readings[i].temp,
                            last_readings[j].temp);
                        new_readings[i].temp = last_readings[j].temp; //restauramos la lectura anterior
                    }
                    break;
                }
            }
        }
    }
    else if (last_readings.empty() && !new_readings.empty()) {
        last_readings = new_readings;
    }

    //chequeamos si hubo cambios respecto a la lectura anterior
    bool changed = readings_changed(last_readings, new_readings);

    if (changed) {
        LOGI(TAG, "Lectura cambio: %s", tempjson.c_str());
    } else {
        LOGN(TAG, "Lectura igual, no se loguea detalle"); //No se guarda log
    }

    onewire_idle = true; //marco que terminé de procesar el comando
    //LOGN(TAG, "Lectura: %s", tempjson.c_str());
    

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