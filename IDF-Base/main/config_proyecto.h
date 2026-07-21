#include "esp_err.h"
#include <string> 
#include <vector>
#include "utils_cmd_dispatcher.h" //para usar dispatcher


bool cargar_config_proyecto_file_directo() ; // Carga directa sin pasar por el dispatcher, para usar en app_main antes de iniciar el dispatcher
