/************************************************************************************* 
* Funciones de conexión y envío de datos a MiTS.com
* 
**************************************************************************************/

#include "cJSON.h"
#include <string>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils_http.h"
#include "utils_mits.h"
#include "MisVariablesProyecto.h"
#include "utils_config.h" //para SensorID
#include "utils_logger.h" //para write_system_log

/************************************************************************************* 
* WriteTBulk, Escribe lo que está en la estructura TSmessageWrite en MiTS.com
* WriteTS(stmensaje, "miTSESP.com", resultado_buf)
**************************************************************************************/
struct TSmessageWrite mensajeWrite;
struct TSmessageRead mensajeRead;
SemaphoreHandle_t g_mits_mutex = nullptr;

void miTS_init()
{
    //g_tsurl = URL_BASE;
    if (!g_mits_mutex)
        g_mits_mutex = xSemaphoreCreateMutex(); //mutex para proteger el acceso a MiTS desde varias tareas

    mensajeWrite.sensor_id = SensorID;
    strcpy(mensajeWrite.field_name[0],"field1");
    strcpy(mensajeWrite.field_name[1],"field2");
    strcpy(mensajeWrite.field_name[2],"field3");
    strcpy(mensajeWrite.field_name[3],"field4");
    strcpy(mensajeWrite.field_name[4],"field5");
    strcpy(mensajeWrite.field_name[5],"field6");
    strcpy(mensajeWrite.field_name[6],"field7");
    strcpy(mensajeWrite.field_name[7],"field8");

    strcpy(mensajeWrite.esquema, esquema);
    strcpy(mensajeWrite.write_api_key, WRITE_API_KEY);

    mensajeRead.sensor_id = SensorID;
    strcpy(mensajeRead.read_api_key, READ_API_KEY);
}

void WriteTSBulk(TSmessageWrite *mensaje,  char *_miURL, char* resultado_buf, int size_resultado_buf) {
    // 1. Preparar la URI (usando std::string)
    //URL: http://carze.pythonanywhere.com/channels/ESP32IDF/bulk_update.json?SensorID=7001

    std::string uriThingSpeak = std::string("/channels/");
    uriThingSpeak += mensaje->esquema;
    uriThingSpeak += "/bulk_update.json?SensorID=";
    uriThingSpeak += std::to_string(mensaje->sensor_id);

    // 2. Obtener el tiempo Epoch (usando time.h estándar de IDF)
    time_t now;
    time(&now);
    long HoraEpochUTC = (long)now + (3 * 3600); // Ajuste GMT+3 como tenías

    // 3. Crear el objeto JSON principal: { ... }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "write_api_key", mensaje->write_api_key); // Suponiendo que _writekey es char* o std::string.c_str()

    // 4. Crear el array "updates": [ ... ]
    cJSON *updates_array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "updates", updates_array);

    // 5. Crear el objeto dentro del array: { "created_at": ..., "fieldX": ..., "status": ... }
    cJSON *update_item = cJSON_CreateObject();
    cJSON_AddNumberToObject(update_item, "created_at", HoraEpochUTC); 
    
    // Para el nombre del campo dinámico (field1, field2, etc)
    char field_name[8];
    for(int i = 0; i < 7; i++) {
        if (strlen(mensaje->field_data[i]) > 0) {
            snprintf(field_name, sizeof(field_name), "field%d", i + 1);
            cJSON_AddStringToObject(update_item, field_name, mensaje->field_data[i]);
        }
        else {
            // Si el campo está vacío,  lo añadimos igual porque miTS lo requiere.
            snprintf(field_name, sizeof(field_name), "field%d", i + 1);
            cJSON_AddStringToObject(update_item, field_name, "");
        }
    }  
    snprintf(field_name, sizeof(field_name), "field8");
    cJSON_AddStringToObject(update_item, field_name, mensaje->field_data8); 
    
    //snprintf(field_name, sizeof(field_name), "field%d", NroCampo);
    //cJSON_AddStringToObject(update_item, field_name, Contenido); // Contenido debe ser char*
    
    cJSON_AddStringToObject(update_item, "status", "Ok");

    // Añadir el objeto al array
    cJSON_AddItemToArray(updates_array, update_item);

    // 6. Generar el string final (compacto para el POST)
    char *mensajeTS = cJSON_PrintUnformatted(root);

    // 7. Ejecutar el POST (usando tu función de IDF)
    ESP_LOGI("TS", "Enviando JSON: %s", mensaje->field_data8);
    // {"write_api_key":"WR4QL85BR9KIBP6V","updates":[{"created_at":1767383230,"field8":"Status:Online=1 nada=algo","status":"Ok"}]}
    //"POST /channels/Esquema1/bulk_update.json?SensorID=25 HTTP/1.1" 200 23 "-" "-" "190.194.119.36"
    //"POST /channels/7001/bulk_update.json HTTP/1.1" 200 172 "-" "ESP32_Client/1.0" "181.117.77.42"
    //
    // Suponiendo que tu función http_perform_post recibe (url, payload, respuesta)
    std::string url_completa = std::string("http://") + _miURL + uriThingSpeak;
    int ret_code = http_perform_post(url_completa.c_str(), mensajeTS, resultado_buf, size_resultado_buf-1, "application/json");
    if (ret_code == ESP_OK) {
        ESP_LOGI("TS", "Envío satisfactorio! Código: %d", ret_code);
        ESP_LOGI("TS", "Respuesta del servidor: %s", resultado_buf);
        //success = true;
    } else {
        ESP_LOGE("TS", "Fallo en el envío. Código HTTP: %d", ret_code);
        if (strlen(resultado_buf) > 0) {
            ESP_LOGW("TS", "Detalle del error: %s", resultado_buf);
        }
    }

    // 8. LIMPIEZA CRÍTICA (Evitar fugas de memoria)
    cJSON_Delete(root); // Borra todo el árbol (incluyendo el array e items)
    free(mensajeTS);    // Libera el string generado por cJSON_Print
}


bool writePost(const char* url,
               std::initializer_list<std::pair<const char*, const char*>> fields,
               TickType_t timeout_ms)
{
    if (!g_mits_mutex) return false;

    // Intentar tomar el mutex
    if (xSemaphoreTake(g_mits_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGW("MITS", "writePost: recurso ocupado, timeout");
        return false;
    }

    // Limpiar todos los fields
    for (int i = 0; i < 8; i++) {
        mensajeWrite.field_data[i][0] = '\0';
    }
    mensajeWrite.field_data8[0] = '\0';

    // Cargar los campos recibidos
    for (auto& p : fields) {
        const char* fname = p.first;
        const char* fdata = p.second;

        for (int i = 0; i < 8; i++) {
            if (strcmp(mensajeWrite.field_name[i], fname) == 0) {
                strcpy(mensajeWrite.field_data[i], fdata);
            }
        }
    }

    // Ejecutar POST
    char response[512] = {0};
    ESP_LOGI("MITS", "POST → %s", url);

    WriteTSBulk(&mensajeWrite, (char*)url, response, sizeof(response));

    ESP_LOGI("MITS", "Respuesta POST: %s", response);

    xSemaphoreGive(g_mits_mutex);
    return true;
}
