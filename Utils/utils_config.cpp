
#include "esp_log.h"
#include <stdio.h>      // Para fopen, fclose, fgets
#include <string.h>     // Para strstr, strcspn, strtok
//using namespace std; //si no, hay que poner std:: en todo y usar string.h
#include <stdlib.h>     // Para atoi
#include "esp_littlefs.h" // Asegurarse de que el FS esté montado antes de llamar a esta función
//#include "utils_files.h" // Para MOUNT_POINT
#include "utils_config.h"
#include "utils_cmd_dispatcher.h" //para usar dispatcher
#include "utils_logger.h"

// Inicialización de las variables globales declardas en el .h
// Definición real de las variables
std::string g_pending_ssid = "";
std::string g_pending_pass = "";
app_config_t g_app_config = {}; // También inicializamos la estructura
bool g_ota_en_progreso = false; // Variable global OTA
//bool wifi_ready = false; //indica si el wifi está vivo y operativo (con IP, no solo conectado a un AP)
//bool g_manual_wifi_connect = false; // Indica si se solicitó una conexión WiFi manual (desde comando) para que el sistema no intente reconectar automáticamente

static const char *TAG_CONFIG = "CONFIG";
// Definición de la variable global (Asignación de memoria, solo aquí)
//app_config_t g_app_config;
// El nombre del archivo de configuración


bool cargar_config_desde_file(CommandDispatcher* disp) {
    // Usamos la macro que definimos antes
    FILE* f = fopen(CONFIG_FILE_PATH, "r"); 
    
    if (f == NULL) {
        ESP_LOGW(TAG_CONFIG, "No se encontró %s. El sistema iniciará con valores default.", CONFIG_FILE_PATH);
        return false;
    }
    char buffer[160];
    std::string respuesta;
    char linea[128];
    while (fgets(linea, sizeof(linea), f)) {
        // 1. Limpieza de caracteres invisibles (\r, \n)
        linea[strcspn(linea, "\r\n")] = 0;
        
        // 2. Saltar comentarios o líneas basura
        if (linea[0] == '#' || strlen(linea) < 3) continue;
        
        snprintf(buffer, sizeof(buffer), "Aplicando: %s", linea);
        respuesta = buffer;
        ESP_LOGI(TAG_CONFIG, "%s", respuesta.c_str());
        
        write_system_log(TAG_CONFIG, respuesta.c_str());
        // 3. Ejecución inmediata
        // Si tu dispatcher tiene un método que devuelve string (como vimos ayer), 
        // puedes incluso loguear la respuesta del comando.
        respuesta = disp->submit(CMD_SRC_SYSTEM, linea);
        write_system_log(TAG_CONFIG,  respuesta.c_str());
        
        ESP_LOGD(TAG_CONFIG, "Respuesta: %s", respuesta.c_str());
        // Si quieres seguir aplicando comandos incluso si uno falla, no retornes false aquí, solo loguea el error dentro del comando que se ejecute. Si quieres que falle toda la carga ante un error, podrías retornar false aquí si la respuesta indica un error.
    }
    
    fclose(f);
    respuesta = "Configuración de inicio completada.";
    write_system_log(TAG_CONFIG,  respuesta.c_str());
    ESP_LOGI(TAG_CONFIG, "%s", respuesta.c_str());
    return true;
}


// Auxiliar: Quita espacios al inicio y final de una cadena
static void trim_whitespace(char *str) {
    size_t len = strlen(str);
    if (len == 0) return;

    // Eliminar espacios al final
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }

    // Si la cadena se vació, retornar
    if (len == 0) return;

    // Eliminar espacios al inicio
    char *start = str;
    while (*start == ' ') {
        start++;
    }

    // Mover la cadena si el inicio fue ajustado
    if (start != str) {
        memmove(str, start, len + 1); // +1 para el terminador nulo
    }
}
/*
esp_err_t load_sensor_config(void) {
    FILE *f = fopen(CONFIG_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGE(TAG_CONFIG, "FALLO al abrir el archivo %s", CONFIG_FILE_PATH);
        return ESP_FAIL;
    }

    char line[128]; // Buffer para cada línea del archivo
    char key[64];
    char value[64];
    esp_err_t ret = ESP_OK;

    // Inicializar la estructura con valores por defecto o cero (buena práctica)
    memset(&g_app_config, 0, sizeof(app_config_t));
    g_app_config.sensor_id_int = -1; // Valor que indica 'no cargado'

    ESP_LOGI(TAG_CONFIG, "Cargando configuración desde %s...", CONFIG_FILE_PATH);

    while (fgets(line, sizeof(line), f) != NULL) {
        
        // 1. Ignorar líneas vacías y comentarios
        trim_whitespace(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        // 2. Buscar el separador '='
        char *eq_sign = strchr(line, '=');
        if (eq_sign == NULL) {
            ESP_LOGW(TAG_CONFIG, "Línea sin separador '=': %s", line);
            continue;
        }

        // 3. Extraer Clave (Key)
        size_t key_len = eq_sign - line;
        if (key_len >= sizeof(key)) {
            ESP_LOGE(TAG_CONFIG, "Clave muy larga en línea: %s", line);
            continue;
        }
        strncpy(key, line, key_len);
        key[key_len] = '\0'; // Terminar la clave
        trim_whitespace(key);

        // 4. Extraer Valor (Value)
        char *val_start = eq_sign + 1;
        if (strlen(val_start) >= sizeof(value)) {
            ESP_LOGE(TAG_CONFIG, "Valor muy largo para la clave %s", key);
            continue;
        }
        strcpy(value, val_start);
        trim_whitespace(value);

        // 5. Mapear y Cargar a la Estructura
        if (strcmp(key, "SENSORID") == 0) {
            strncpy(g_app_config.sensor_id_str, value, sizeof(g_app_config.sensor_id_str) - 1);
            g_app_config.sensor_id_int = atoi(value); // Convertir a entero
        } else if (strcmp(key, "miTSurl") == 0) {
            strncpy(g_app_config.api_endpoint, value, sizeof(g_app_config.api_endpoint) - 1);
        } else if (strcmp(key, "miTSPort") == 0) {
            // Asumo que tienes un campo en la estructura para el puerto
            // g_app_config.api_port = atoi(value); 
        } else if (strcmp(key, "WRITEKE") == 0) {
            strncpy(g_app_config.write_key, value, sizeof(g_app_config.write_key) - 1);
        } else if (strcmp(key, "READKEY") == 0) {
            strncpy(g_app_config.read_key, value, sizeof(g_app_config.read_key) - 1);
        } else if (strcmp(key, "MICANALT") == 0) {
            // Asumo que tienes un campo para el nombre del canal
            // strncpy(g_app_config.channel_name, value, sizeof(g_app_config.channel_name) - 1);
        } 
        // Nota: Las claves 'fconfig' y 'fstatus' no se cargan a la estructura, pero se ignoran con seguridad.

        ESP_LOGD(TAG_CONFIG, "Parsed KEY: %s, VALUE: %s", key, value);
    }

    fclose(f);

    // 6. Validación (Opcional pero Recomendado)
    if (g_app_config.sensor_id_int == -1) {
        ESP_LOGE(TAG_CONFIG, "Error: SENSORID no fue encontrado o es inválido.");
        ret = ESP_FAIL;
    }

    return ret;
}



// Implementación de la función accesora
int get_sensor_id(void) {
    return g_device_sensor_id;
}

// Implementación de la función de carga
esp_err_t load_sensor_config(void) {
    // 1. Abrir el archivo
    FILE *f = fopen(CONFIG_FILE_PATH, "r"); // fs/config.txt
    //FILE *f = fopen(MOUNT_POINT "/index.html", "r");
    if (f == NULL) {
        ESP_LOGE(TAG_CONFIG, "Fallo al abrir el archivo %s. ¿FS montado?", CONFIG_FILE_PATH);
        return ESP_FAIL;
    }
    
    char line[128];
    
    // 2. Leer línea por línea
    while (fgets(line, sizeof(line), f) != NULL) {
        // Buscar la línea que contiene el token de interés
        if (strstr(line, "SENSORID=") != NULL) {
            // Eliminar salto de línea o retorno de carro
            line[strcspn(line, "\r\n")] = 0; 
            
            // 3. Parsear el valor después del '='
            char *token_value = strchr(line, '=');
            if (token_value != NULL) {
                // Mover el puntero al inicio del valor (después del '=')
                token_value++; 
                
                // 4. Convertir el valor a entero y almacenarlo
                g_device_sensor_id = atoi(token_value);
                
                ESP_LOGI(TAG_CONFIG, "CONFIGURACION OK: SENSORID cargado como %d", g_device_sensor_id);
                fclose(f);
                return ESP_OK;
            }
        }
    }

    fclose(f);
    ESP_LOGE(TAG_CONFIG, "Error: SENSORID no encontrado o archivo vacío.");
    return ESP_ERR_NOT_FOUND;
}
*/