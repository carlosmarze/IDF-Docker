#include "sched_tasks.h"
#include "esp_log.h"
#include <string>
#include <cstring>

#include "utils_http.h"
#include "utils_wifi.h"
#include "utils_test.h"
#include "utils_mits.h"
#include "utils_time.h"
#include "utils_mits.h"
#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_config.h"
#include "utils_logger.h"
#include "MisVariablesProyecto.h"

static const char* TAG = "SCHED_TASKS";
static const char* g_tsurl = URL_BASE;

// Estado compartido que antes estaba dentro de http_test_task
static bool g_first_run = true;
static char g_response_web[512] = {0};
//static const char* g_tsurl = nullptr;

// mensajeWrite / mensajeRead vienen de utils_mits.h (como antes)
extern TSmessageWrite mensajeWrite;
extern TSmessageRead  mensajeRead;



// ------------------------
// 60s: GET keepalive + TSComm
// ------------------------
void set_sched_inicio(bool esinicio) {
    g_first_run = esinicio;
}

void task_60()
{
    //se ejecuta desde el inicio y luego cada 60s
    //keepalive y extracción de comandos web (TSComm)

    std::string log_msg;
    std::string get_url3 = std::string("http://") + g_tsurl +
        "/field/field8?api_key=" + mensajeRead.read_api_key +
        "&SensorID=" + std::to_string(mensajeRead.sensor_id);

    ESP_LOGI(TAG, "GET keepalive: %s", get_url3.c_str());

    if (http_perform_get(get_url3.c_str(), g_response_web, sizeof(g_response_web)) == ESP_OK) {
        ESP_LOGI(TAG, "Keepalive OK. Respuesta: %s", g_response_web);

        std::string resp(g_response_web);

        size_t pos = resp.find(CMDWEBPREFIX);
        if (pos != std::string::npos) {
            std::string cmd_str = resp.substr(pos + strlen(CMDWEBPREFIX));
            LOGI(TAG, "Comandos web detectados: '%s'", cmd_str.c_str());
            process_commands(
                CMD_SRC_UART,
                cmd_str,
                0,
                0,
                -1
            );

            // limpiar field8
            strcpy(mensajeWrite.field_data8, "");
            ESP_LOGI(TAG, "Borrando field8");
            WriteTSBulk(&mensajeWrite, (char*)g_tsurl, g_response_web, sizeof(g_response_web));
        }

    } else {
        LOGE(TAG, "Fallo GET keepalive: %s Error: %s", get_url3.c_str(), g_response_web);
       
        ESP_LOGW(TAG, "Error: %s", g_response_web);
    }

    // g_first_run = false;
}

// ------------------------
// 3600s: POST de estado
// ------------------------
void task_3600_post()
{
    //Post una vez por hora el estado del dispositivo (incluso en la primera ejecución al inicio, para que quede un registro en Mits desde el arranque)
    // Generar el mensaje de estado (field8)

    std::string log_msg;
    std::string temp;

    temp  = "Status=Online ";
    temp += " IP=" + get_local_ip();
    temp += " MAC=" + get_mac_address();
    temp += " SSID=" + get_connected_ssid();
    temp += " HR=" + get_current_time();
    temp += " Ver" + std::string(version_info);
    temp += " SunRise=00:00:00";
    temp += " SunSet=00:00:00";
    temp += " Tmp=0";
    temp += " Hum=0";
    temp += " Wind=0";

    ESP_LOGI(TAG, "POST estado (cada 60 min)");
    ESP_LOGI(TAG, "Mensaje8=%s", temp.c_str());
    //WriteTSBulk(&mensajeWrite, (char*)g_tsurl, g_response_web, sizeof(g_response_web));
    // Llamada limpia usando writePost()
    bool ok = writePost(
        g_tsurl,
        {
            {"field1", g_first_run ? "Iniciando" : "Idle"},
            {"field2", "Idle"},
            {"field8", temp.c_str()}
        },
        5000   // timeout para tomar el mutex
    );

    if (!ok) {
        LOGW(TAG, "POST estado NO enviado (mutex ocupado + 5seg)");
        return;
    }

    LOGI(TAG, "POST enviado. Respuesta: %s", g_response_web);
   
}


// ------------------------
// 3600s: status + OTA
// ------------------------
void task_3600_updt() 
{
    //Update OTA cada 60min (no lo hace en la primera ejecución, porque se hizo antesde arrancar el scheduler)
   
    if(g_first_run){
        return; // Si no es la primera ejecución, salimos para que esta tarea solo haga su lógica de inicio en la primera ejecución. El resto de las ejecuciones serán manejadas por el scheduler normalmente.
    }
    std::string log_msg;

    process_commands(CMD_SRC_UART, "status", ' ', ',');

    std::string url_ota = urlUpdateDef;
    url_ota += "?VERSION=";
    url_ota += version_info;
    url_ota += "&SensorID=";
    url_ota += std::to_string(SensorID);
    url_ota += "&MAC=";
    url_ota += get_mac_address();

    snprintf(urlUpdate, sizeof(urlUpdate), "%s", url_ota.c_str());

    std::string cmd_ota_str = R"({
        "cmd": "ota",
        "arg": {
            "server": ")" + std::string(urlUpdate) + R"(",
            "reboot": "yes",
            "chunksize": "2048",
            "https": "yes"
        }
    })";

    log_msg = "comando OTA generado: " + cmd_ota_str;
    ESP_LOGI(TAG, "%s", log_msg.c_str());

    process_commands(CMD_SRC_UART, cmd_ota_str.c_str(), ' ', ',');
}
