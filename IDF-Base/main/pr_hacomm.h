#pragma once
// Comunicación con Home Assistant, para calefacción de pileta, con sensores de temperatura DS18B20 y comunicación con miTS
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "utils_config.h"

#define FILE_HA_CONFIG MOUNT_POINT "/ha_config.json" // Archivo de configuración para Home Assistant
#define TOPIC_HA_BASE "miTSESP/HA/" // Base del topic para Home Assistant El tóopico a mandar debería ser miTSESP/HA/<SensorID>/temperatura, miTSESP/HA/<SensorID>/relay, etc. según lo que se quiera enviar a Home Assistant
#define HA_DISCOVERY_PREFIX  "homeassistant/sensor/"
#define HA_DEVICE_ID         "Calefac_pileta"
#define HA_SENSOR_COUNT      4

#define SENSORSCFG "/sensorscfg.txt"
#define MAX_SENSORS 4

typedef struct {
    char id[32];     // ID del sensor físico
    float temp;      // última lectura
    bool valid;      // si se encontró en el JSON
} temp_slot_t;

static temp_slot_t temp_slots[MAX_SENSORS];
static bool ha_config_loaded = false; // Indica si la configuración de HA se ha cargado correctamente

void ha_init(); //inicialización
bool is_ha_config_loaded(); //Consulta el status de la inicialización
void map_sensors_by_id(const char *json); //Publica los valores de temperatura a Home Assistant según el JSON recibido del comando tempscan
