#include "esp_err.h"
#include <string> 
#include <vector>
#include "utils_cmd_dispatcher.h" //para usar dispatcher

#define ONEWIRE_DEFAULT_PIN GPIO_NUM_4 // Valor por defecto para el pin OneWire
extern int g_onewire_pin; //pin donde están conectados los sensores DS18B20. Se puede cambiar con el comando "temp_scan pin=<gpio>".
extern std::vector<std::array<uint8_t, 8>> g_sensors;
extern bool onewire_init; // Variable global para indicar si el escaneo de OneWire se ha completado
extern std::string tempjson; //json con la lectura de la temperatura
bool cargar_config_proyecto_file_directo() ; // Carga directa sin pasar por el dispatcher, para usar en app_main antes de iniciar el dispatcher