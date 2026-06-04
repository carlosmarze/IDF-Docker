#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>

// ESP-IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "esp_sntp.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"

// Utils y módulos del proyecto
#include "utils_wifi.h"
#include "utils_mqtt.h"
#include "utils_files.h"
#include "utils_webs.h"
#include "utils_ntp.h"
#include "utils_time.h"
#include "utils_http.h"
#include "utils_mits.h"
#include "utils_config.h"
#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_cmd_set.h"
#include "utils_ota_worker.h"
#include "utils_logger.h"
#include "utils_sched.h"
#include "utils_events.h"
#include "sched_tasks.h"
#include "MisVariablesProyecto.h"

#define TAG "APP_MAIN"
#define PAYLOAD_SIZE 128

// Variables globales del proyecto
//int SensorID;
char urlUpdate[128];
QueueHandle_t command_queue = nullptr;
//static bool first_run_done = false;
extern CommandDispatcher dispatcher;
extern CommandDispatcher* global_dispatcher_ptr;
// Estructura de datos para la comunicación entre tareas
typedef struct {
    int valor;
} DatosSensor_t;


// ============================================================
//  COMANDO "bomba" (vive en este archivo, como siempre)
// ============================================================
class bomba : public Command {
public:
    const char* name() const override { return "bomba"; }
    const char* usage() const override { return "bomba [On/Off]"; }
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        ESP_LOGI("bomba", "Comando recibido: %s", args[0].c_str());
        return "Comando recibido: " + args[0];
    }
};


// ============================================================
//  TAREAS FREERTOS (todas las que ya tenías)
// ============================================================
void tarea_procesamiento(void* pvParameters) {
    DatosSensor_t datos;
    char payload[PAYLOAD_SIZE];

    while (1) {
        if (xQueueReceive(command_queue, &datos, portMAX_DELAY) == pdPASS) {
            int valor = datos.valor * 2;
            sprintf(payload, "{\"sensor_value\": %d}", valor);
        }
    }
}

void tarea_lector_sensor(void* pvParameters) {
    char payload_raw[PAYLOAD_SIZE];

    while (1) {
        int lectura = rand() % 100;

        DatosSensor_t d;
        d.valor = lectura;
        xQueueSend(command_queue, &d, pdMS_TO_TICKS(50));

        sprintf(payload_raw, "%d", lectura);
        vTaskDelay(pdMS_TO_TICKS(3600000));
    }
}

void check_ntp_status_task(void *pvParameter) {
    while (1) {
        ntp_sync_status_t status = get_ntp_status();
        if (status == NTP_STATUS_SYNCHRONIZED)
            ESP_LOGI("APP", "NTP OK");
        else
            ESP_LOGW("APP", "NTP FALLÓ");

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}


// ============================================================
//  service_starter_task (vive en main.cpp, como siempre)
// ============================================================
void service_starter_task(void *pvParameters)
{
    write_system_log("SYS", "Service Starter: Esperando resolución de OTA...");

    int timeout_ota = 0;
    while (g_ota_en_progreso && timeout_ota < 120) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        timeout_ota++;
    }

    if (g_ota_en_progreso) {
        write_system_log("SYS", "OTA excedió tiempo. Liberando servicios.");
        g_ota_en_progreso = false;
    }

    write_system_log("SYS", "Service Starter activo.");

    //static bool first_run_done = false;
    static bool web_started = false;
    static bool aux_tasks_started = false;

    while (1) {

        if (!wifi_ready) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (g_ota_en_progreso) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!mqtt_is_initialized()) {
            ESP_LOGI("SERVICE", "WiFi OK. Arrancando MQTT...");
            mqtt_app_start();
        }

        if (!web_started) {
            start_webserver(global_dispatcher_ptr);
            web_started = true;
        }

        if (!aux_tasks_started) {
            xTaskCreate(check_ntp_status_task, "ntp_status_check", 4096, NULL, 5, NULL);
            aux_tasks_started = true;
        }

        if (!first_run_done) {
            ESP_LOGI("SERVICE", "FIRST RUN...");

            //set_sched_inicio(true); //lo movemos antes de arrancar scheduler en app_task
            task_60();
            task_3600_post();
            task_3600_updt();
            set_sched_inicio(false);

            first_run_done = true;
            ESP_LOGI("SERVICE", "FIRST RUN completado.");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


// ============================================================
//  app_task — REORDENADO EN FASES
// ============================================================
void app_task(void *pv)
{
    // ------------------------------------------------------------
    // FASE 0 — Infraestructura base
    // ------------------------------------------------------------
    littlefs_init();
    init_logger_system();
    //init_system_events();
    if(!cargar_config_desde_file_directo()) {
        std::string log_msg = "No se encontró " + std::string(CONFIG_FILE_PATH) + ". El sistema iniciará con valores default.";
        write_system_log("SYS", log_msg.c_str());    
        ESP_LOGW("SYS", "%s", log_msg.c_str());    
        SensorID = SENSORID; //config.txt es un archivo con una lista de comandos, por ej setsensorid=7001
    } else {
        write_system_log("SYS", "Configuración cargada desde el archivo.");
    }

    std::string log_msg = "\n\nINICIANDO SISTEMA - VERSION: " + std::string(version_info) + ", SensorID: " + std::to_string(SensorID) + ", Heap Libre: " + std::to_string(esp_get_free_heap_size()) + " bytes";
    write_system_log("APP_MAIN", log_msg.c_str());
    ESP_LOGI("APP_MAIN", "%s", log_msg.c_str());

    setenv("TZ", "ART3", 1);
    tzset();

    // ------------------------------------------------------------
    // FASE 1 — NVS + wifi.json
    // ------------------------------------------------------------
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    struct stat st;
    if (stat(WIFI_JSON_FILE, &st) != 0) {
        write_system_log("SYS", "wifi.json no detectado. Creando inicial...");
        save_wifi_network(SSIDDEFAULT, PASSDEFAULT);
    }

    // ------------------------------------------------------------
    // FASE 2 — Red + WiFi
    // ------------------------------------------------------------
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_hardware_init();

    xTaskCreate(wifi_check_task, "wifi_check", 4096, NULL, 5, NULL);
    xTaskCreate(wifi_connect_task, "wifi_connect_task", 8192, NULL, 5, &wifi_connect_task_handle);

    // ------------------------------------------------------------
    // FASE 3 — Sistema de comandos
    // ------------------------------------------------------------
    command_queue = xQueueCreate(10, sizeof(cmd_msg_t));

    global_dispatcher_ptr = &dispatcher;
    dispatcher.start();

    register_utils_commands(dispatcher);
    dispatcher.registerCommand(std::make_unique<bomba>());

    xTaskCreate([](void* arg) {
        static_cast<CommandDispatcher*>(arg)->dispatcherTask();
    }, "dispatcher_task", 4096, global_dispatcher_ptr, 5, NULL);

   

    // ------------------------------------------------------------
    // FASE 4 — Conexión maestra
    // ------------------------------------------------------------
    iniciar_proceso_conexion_maestra();

    // ------------------------------------------------------------
    // FASE 5 — NTP si hay WiFi
    // ------------------------------------------------------------
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) {
        ntp_client_init(1800, nullptr, "ART3");

        int retry_ntp = 0;
        while (time(nullptr) < 1000000000 && retry_ntp++ < 15)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    //Todo lo que va antes que esto tiene que ser SUPER estable porque si no el dispositivo puede quedar inutilizado sin forma de arreglarlo salvo reflasheando manualmente. Por eso el orden de las fases es tan importante, y por eso la fase OTA va al final, para que el dispositivo llegue a esa fase con todo lo demás funcionando bien y con la configuración cargada, para minimizar riesgos de fallos en OTA.
    // ------------------------------------------------------------
    // FASE 6 — OTA
    // ------------------------------------------------------------
    /*
    std::string url_ota = urlUpdateDef;
    url_ota += "?VERSION=" + std::string(version_info);
    url_ota += "&SensorID=" + std::to_string(SensorID);
    url_ota += "&MAC=" + get_mac_address();

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
    */
    ota_worker_start(); //arranca la tarea y la cola de OTA, que queda esperando a que le submitan un trabajo para arrancar el proceso OTA. El comando OTA no hace el proceso OTA directamente, sino que le notifica a esta tarea para que lo haga, y así evitar cualquier posible bloqueo o retraso en el proceso OTA que pueda ocurrir al pasar por el dispatcher y sus colas, y además darle máxima prioridad al proceso OTA, que es crítico para el dispositivo.

    bits = xEventGroupGetBits(s_wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) { //Si no se conectó antes, no hace OTA
        
        std::string url_ota = urlUpdateDef;
        url_ota += "?VERSION=" + std::string(version_info);
        url_ota += "&SensorID=" + std::to_string(SensorID);
        url_ota += "&MAC=" + get_mac_address();
        std::string log_msg = "URL OTA generada: " + url_ota;
        ESP_LOGI("OTA", "%s", log_msg.c_str());
        write_system_log("OTA", log_msg.c_str());
            
        //en lugar de pasar por el dispatcher, que es lo que haría process_commands, vamos a submitir directo a la cola de OTA para minimizar riesgos de fallos en el proceso OTA, que es crítico y queremos que tenga la máxima prioridad y el menor número de puntos de fallo posible, además de evitar cualquier posible retraso o bloqueo que pueda ocurrir al pasar por el dispatcher y sus colas.
        //process_commands(CMD_SRC_UART, cmd_ota_str.c_str(), ' ', ',');
        ota_job_t job = {};
        snprintf(job.url, sizeof(job.url), "%s", url_ota.c_str());    
        strncpy(job.sha256_expected, "", sizeof(job.sha256_expected));
        job.reboot_after = true;
        job.partial_download = true;
        job.chunk_size = 2048;
        job.https = true;
        ota_submit(&job); //submitimos directo, sin pasar por el dispatcher para minimizar riesgos de fallos en el proceso OTA, que es crítico y queremos que tenga la máxima prioridad y el menor número de puntos de fallo posible, además de evitar cualquier posible retraso o bloqueo que pueda ocurrir al pasar por el dispatcher y sus colas.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ------------------------------------------------------------
    // FASE 7 — Scheduler + servicios del proyecto
    // ------------------------------------------------------------
    miTS_init();

    sched_register_task_60(task_60);
    sched_register_task_3600_post(task_3600_post);
    sched_register_task_3600_updt(task_3600_updt);

    while (g_ota_en_progreso) {
        ESP_LOGI(TAG, "Esperando que termine OTA...");
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
    
    set_sched_inicio(true);
    start_scheduler();
    
    xTaskCreate(service_starter_task, "srv_start", 8192, NULL, 5, NULL); //aquí se resetea el flag de primera ejecución para que las tareas del scheduler hagan su lógica de inicio correctamente en la primera ejecución, y luego se setea a false para que el resto de las ejecuciones sean manejadas normalmente por el scheduler.

    ESP_LOGI("APP_MAIN", "Fin Init. Heap libre: %d", esp_get_free_heap_size());

    // ------------------------------------------------------------
    // FASE 8 — Loop principal
    // ------------------------------------------------------------
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        if (!g_ota_en_progreso)
            ESP_LOGI(TAG, "Main loop activo. Heap libre: %u", esp_get_free_heap_size());
    }
}


// ============================================================
//  app_main — limpio
// ============================================================
extern "C" void app_main(void)
{
    xTaskCreate(app_task, "app_task", 12288, NULL, 5, NULL);
}
