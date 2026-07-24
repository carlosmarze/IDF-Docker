#include "esp_err.h"
#include <string> 
#include <vector>
#include "utils_cmd_dispatcher.h" //para usar dispatcher

#define TEMPTOLERANCIA 0.2f // tolerancia de temperatura para considerar que cambió y hay que enviar a miTS
#define TEMPMAXDELTA 10.0f // tolerancia máxima de temperatura para considerar que hay un error de lectura y no enviar a miTS
bool cargar_config_proyecto_file_directo() ; // Carga directa sin pasar por el dispatcher, para usar en app_main antes de iniciar el dispatcher
