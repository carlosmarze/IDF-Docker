
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <cstring>

#include "utils_http.h"
#include "utils_wifi.h"
#include "utils_test.h"
#include "utils_mits.h"
#include "utils_time.h"
#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_config.h"
#include "utils_logger.h"

#include "MisVariablesProyecto.h" // Archivo con variables específicas del proyecto
extern CommandDispatcher dispatcher; // Dispatcher global para comandos, definido en main.cpp
#define TAG "MAIN_LOOP"
// 6. Probar petición HTTP (opcional) La task se arranca desde main.cpp
void http_test_task(void *pvParameters) {
    //std::string log_msg;
    
   
   const char *tsurl = URL_BASE; //url base para miTS, definida en MisVariablesProyecto.h
    mensajeWrite.sensor_id = SensorID;
    //nombres de los campos no cambian, son siempre los mismos
    strcpy(mensajeWrite.field_name[0],"field1");
    strcpy(mensajeWrite.field_name[1],"field2");
    strcpy(mensajeWrite.field_name[2],"field3");
    strcpy(mensajeWrite.field_name[3],"field4");
    strcpy(mensajeWrite.field_name[4],"field5");
    strcpy(mensajeWrite.field_name[5],"field6");
    strcpy(mensajeWrite.field_name[6],"field7");
    strcpy(mensajeWrite.field_name[7],"field8"); 


    strcpy(mensajeWrite.esquema, ESQUEMA);
    strcpy(mensajeWrite.write_api_key, WRITE_API_KEY);
    
    
    mensajeRead.sensor_id = SENSORID;
    strcpy(mensajeRead.read_api_key, READ_API_KEY);

    char response_web[512] = {0}; //acá quedará la respuesta del servidor
    // Definimos los intervalos en milisegundos
    const uint32_t interval_get  = 60 * 1000;      // 60 segundos
    const uint32_t interval_post = 60 * 60 * 1000; // 60 minutos
    const uint32_t interval_updt = 60 * 60 * 1000; // 60 minutos
    // Guardamos el tiempo de la última ejecución
    TickType_t last_get_time = xTaskGetTickCount();
    TickType_t last_post_time = xTaskGetTickCount();
    TickType_t last_updt_time = xTaskGetTickCount();
    bool first_run = true;
   
    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        

        // --- LÓGICA PARA EL POST (Cada 10 min) ---
        if (first_run || (current_time - last_post_time) >= pdMS_TO_TICKS(interval_post)) {
            
            last_post_time = current_time;
            //generamos los datos que van en los campos que vamos a enviar

            if(first_run){
                strcpy(mensajeWrite.field_data[0],"Iniciando");

            }
            else {
                strcpy(mensajeWrite.field_data[0],"Idle");
            }
            
            strcpy(mensajeWrite.field_data[1],"Idle"); 
            strcpy(mensajeWrite.field_data[2],"");
            strcpy(mensajeWrite.field_data[3],"");
            strcpy(mensajeWrite.field_data[4],"");
            strcpy(mensajeWrite.field_data[5],"");
            strcpy(mensajeWrite.field_data[6],"");
            //generamos el campo 8 con info de estado
            //miTS espera que el campo8 tenga 
            //mens="HR:{} SSID {} MAC:{} IP:{} Ver:{} SunR:{} SunS:{} Tmp:{} Hum:{} Wind:{}".
            //format(diccionario["HR"], diccionario["SSID"], diccionario["MAC"], diccionario["IP"], diccionario["Version"], 
            //diccionario["SunRise"], diccionario["SunSet"], diccionario["Tmp"], diccionario["Hum"], diccionario["Wind"])
            
            std::string temp= "Status=Online ";
            temp += " IP=";
            temp +=  get_local_ip();
            temp += " MAC=";
            temp += get_mac_address();
            temp += " SSID=";
            temp += get_connected_ssid();
            temp += " HR=";
            temp += get_current_time();
            temp += " Ver";
            temp += version_info;
            temp += " SunRise=";
            //temp += get_sunrise_time();
            temp += "00:00:00"; //por ahora no tengo la función lista
            temp += " SunSet=";
            temp += "00:00:00"; //por ahora no tengo la función lista
            //temp += get_sunset_time();
            temp += " Tmp=";
            temp += "0"; //por ahora no tengo la función lista
            //temp += std::to_string(get_temperature());
            temp += " Hum=";
            temp += "0"; //por ahora no tengo la función lista
            //temp += std::to_string(get_humidity());
            temp += " Wind=";
            temp += "0"; //por ahora no tengo la función lista
            //temp += std::to_string(get_wind_speed());

            printf("Mensaje8=%s\n", temp.c_str());
            strcpy(mensajeWrite.field_data8, temp.c_str());


            ESP_LOGI(TAG, "Ejecutando POST de 10 min");
            
            WriteTSBulk(&mensajeWrite, (char *)tsurl, response_web, sizeof(response_web));
            
            ESP_LOGI(TAG, "POST a MiTS completado. Respuesta: %s", response_web);
        }

        // --- LÓGICA PARA EL GET (Cada 60s) ---
        if (first_run || (current_time - last_get_time) >= pdMS_TO_TICKS(interval_get)) {
            first_run = false;
            last_get_time = current_time;

            //const char *get_url3 = "http://miTSESP.com/keepalive?sensor_id=7001";
           
            std::string get_url3 = std::string("http://") + tsurl + "/field/field8?api_key=" + mensajeRead.read_api_key + "&SensorID=" + std::to_string(mensajeRead.sensor_id);
            ESP_LOGI(TAG, "Realizando GET de keepalive a: %s", get_url3.c_str());
            if (http_perform_get(get_url3.c_str(), response_web, sizeof(response_web)) == ESP_OK) {
                ESP_LOGI(TAG, "Keepalive exitoso. Respuesta: %s", response_web);

                // ============================================================
                // EXTRAER COMANDOS TSComm, si es que vienen en el campo8 de la respuesta
                // ============================================================
                std::string resp(response_web);   // convertir buffer a std::string

                // Buscar "TSComm,"
                size_t pos = resp.find(CMDWEBPREFIX);
                if (pos != std::string::npos) {

                    // Extraer todo lo que viene después de TSComm,
                    std::string cmd_str = resp.substr(pos + strlen(CMDWEBPREFIX));
                    LOGI(TAG, "Comandos web detectados: '%s'", cmd_str.c_str());
                    //write_system_log(TAG, log_msg.c_str());
                    //ESP_LOGI(TAG, "%s", log_msg.c_str());
                    //ESP_LOGI("MAIN_HTTP", "Comandos web detectados: '%s'", cmd_str.c_str());

                    // Ejecutar comandos usando el parser universal
                    process_commands(
                        CMD_SRC_UART,   // origen
                        cmd_str,       // línea completa con comandos
                        0,             // auto-detectar separador 1
                        0,             // auto-detectar separador 2
                        -1             // sin client_fd
                    );
                    //borramos field8 para que no quede el comando pegado en el mensaje de estado siguiente
                    strcpy(mensajeWrite.field_data8,"");
                    ESP_LOGI(TAG, "Borrando field8");
                    WriteTSBulk(&mensajeWrite, (char *)tsurl, response_web, sizeof(response_web));  
                }

            } else {
                LOGI(TAG, "Fallo la petición GET de keepalive: %s Error: %s", get_url3.c_str(), response_web);
                //write_system_log(TAG, log_msg.c_str());
                //ESP_LOGE(TAG, "%s", log_msg.c_str());
                //LOGW(TAG, "Error: %s", response_web);
            }

        }
        // --- LÓGICA PARA EL update (Cada 60min) ---
        if (first_run || (current_time - last_updt_time) >= pdMS_TO_TICKS(interval_updt)) {
            // tiremos un status para que quede en log una vez por hora
            process_commands(CMD_SRC_UART, "status", ' ', ',');
            //tambien un chequeo OTA
            first_run = false;
            last_updt_time = current_time;
            std::string url_ota = urlUpdateDef;
            url_ota += "?VERSION=";
            url_ota += version_info;
            url_ota += "&SensorID=";
            url_ota += std::to_string(SensorID);
            url_ota += "&MAC=";
            url_ota += get_mac_address();
            // url_ota += '\0'; // Asegurar terminación nula No hace falta porque std::string ya maneja eso, pero no está de más para seguridad en C.

            //snprintf(urlUpdate, sizeof(urlUpdate),
            //       "https://carze.pythonanywhere.com/update?VERSION=%s&SensorID=%d&MAC=%s",
                //     version_info, SensorID, get_mac_address().c_str());
            snprintf(urlUpdate, sizeof(urlUpdate),url_ota.c_str());  
            std::string cmd_ota_str = R"({
                "cmd": "ota",
                "arg": {
                    "server": ")";
            cmd_ota_str += urlUpdate;
            cmd_ota_str += R"(",
                    "reboot": "yes",
                    "chunksize": "2048",
                    "https": "yes"
                }
            })";

            //log_msg = "comando generado: " + cmd_ota_str;
            //write_system_log(TAG, log_msg.c_str());
            LOGI(TAG, "comando generado: %s", cmd_ota_str.c_str());
            
           // ESP_LOGI(TAG, "comando generado: %s", cmd_ota_str.c_str());
            process_commands(CMD_SRC_UART, cmd_ota_str.c_str(), ' ', ',');
            
            
        }

        
        // Esperar un tiempo corto (ej. 1 segundo) para no saturar la CPU revisando
        vTaskDelay(pdMS_TO_TICKS(1000));
       // vTaskDelay(pdMS_TO_TICKS(60000)); // Esperar 1 minuto
    }
}
