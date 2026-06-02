#include "utils_cmd_webupdate.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include <string>
#include <vector>
#include <sstream>
#include <cstdio>
#include "MisVariablesProyecto.h" //para URL_BASE, ESQUEMA, WRITE_API_KEY, READ_API_KEY, SENSORID
#include"utils_webs.h"
#include "utils_logger.h"
//#include "utils_cmd_dispatcher.h"
//#include "utils_cmd_processor.h"    
#include "utils_config.h"
#include "utils_http.h"

static const char* TAG = "WEBUPD";


/*
[1] GET lista de archivos (Dnld=...)
[2] Parsear lista
[3] Para cada archivo:
      - Construir URL remota
      - Construir path local
      - Crear directorios
      - Si Refresh=false:
            comparar tamaño remoto vs local
            si iguales → NO descargar
      - Descargar archivo
      - Guardar en LittleFS
[4] Si el servidor web está iniciado:
      detenerlo
      reiniciarlo

*/


// ======================================================
// 1. Parsear lista Dnld=...
// ======================================================
std::vector<std::string> parse_dnld_list(const std::string& body)
{
    std::vector<std::string> files;
    std::stringstream ss(body);
    std::string token;

    while (ss >> token) {  // separa por espacios automáticamente
        if (token.rfind("Dnld=", 0) == 0) {
            files.push_back(token.substr(5)); // quitar "Dnld="
        }
    }

    return files;
}


// ======================================================
// 2. Crear directorios recursivamente
// ======================================================
void ensure_dirs(const char* path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

// ======================================================
// 3. Obtener tamaño remoto (HEAD)
// ======================================================
int http_get_remote_size(const std::string& url)
{
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }

    esp_http_client_fetch_headers(client);
    int size = esp_http_client_get_content_length(client);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return size;
}

// ======================================================
// 4. Comparar tamaño local vs remoto
// ======================================================
bool should_download(const char* local_path, int remote_size, bool refresh)
{
    if (refresh) return true;

    struct stat st;
    if (stat(local_path, &st) != 0) return true; // no existe

    return st.st_size != remote_size;
}

// ======================================================
// 5. Descargar archivo remoto
// ======================================================
bool download_file(const std::string& url, const char* local_path)
{
    esp_http_client_config_t config = {
        .url = url.c_str(),
        .timeout_ms = 8000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }
    
    int total_bytes = esp_http_client_fetch_headers(client);
    if (total_bytes == 0) {
    ESP_LOGE(TAG, "ERROR: archivo vacío %s", url.c_str());
    return false;
    }


    FILE* f = fopen(local_path, "wb");
    if (!f) {
        esp_http_client_cleanup(client);
        return false;
    }

    char buf[1024];
    int len;

    while ((len = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, len, f);
    }

    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return true;
}

// ======================================================
// 6. Procesar un archivo individual
// ======================================================
bool process_file(const std::string& base_url,
                  const std::string& remote_path,
                  bool refresh)
{
    char local_path[256];
    snprintf(local_path, sizeof(local_path), "%s/%s", MOUNT_POINT, remote_path.c_str());

    ensure_dirs(local_path);
    
    std::string full_url = base_url + remote_path;
    printf("full_url: %s\n", full_url.c_str());
    int remote_size = http_get_remote_size(full_url);
    if (remote_size < 0) {
        ESP_LOGE(TAG, "No se pudo obtener tamaño remoto de %s", full_url.c_str());
        return false;
    }

    if (!should_download(local_path, remote_size, refresh)) {
        ESP_LOGI(TAG, "SKIP %s (mismo tamaño)", full_url.c_str());
        return true;
    }

    ESP_LOGI(TAG, "Descargando %s (%d bytes) a local %s", full_url.c_str(), remote_size, local_path);

    return download_file(full_url, local_path);
}

// ======================================================
// 7. Tarea en background
// ======================================================
static void update_web_task(void *param)
{
    bool refresh = (bool)param;

    ESP_LOGI(TAG, "Iniciando actualización web. Refresh=%d", refresh);

    // 1. Obtener lista
    //http://carze.pythonanywhere.com/field/html?api_key=REZAQ4BH81OQP9PZ&SensorID=25	
    //#define urlFilesUpdate "https://carze.pythonanywhere.com/field/html" esto sería urlFilesUpdate
    //std::string get_url3 = std::string("http://") + tsurl + "/field/field8?api_key=" + mensajeRead.read_api_key + "&SensorID=" + std::to_string(mensajeRead.sensor_id);
    std::string body = http_get(std::string(URL_FILES_UPDATE) + "?api_key=" + READ_API_KEY + "&SensorID=" + std::to_string(SENSORID));
    //Dnld=css/Control.OSMGeocoder.css Dnld=css/images/geocoder.png Dnld=css/SmartSwitch.css Dnld=index.html Dnld=js/Control.OSMGeocoder.js Dnld=js/Mapa.js Dnld=js/SmartSched.js Dnld=js/SmartSProc.js Dnld=js/SmartSwitch.js Dnld=Mapa.html Dnld=Sched/SchedFile1.txt Dnld=Sched/SchedFile2.txt Dnld=Sched/SchedFileC1.txt Dnld=Sched/SchedFileC2.txt Dnld=Schedule.html
    auto files = parse_dnld_list(body);

    // 2. Detener servidor web
    if (server_running()) {
        stop_webserver();

        ESP_LOGI(TAG, "Deteniendo servidor web...");
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // 3. Procesar archivos
    for (auto &f : files) {
        //https://carze.pythonanywhere.com/getfile/zip?File=25/Sched/SchedFileC1.txt
        std::string url = "http://" + std::string(URL_BASE) + "/getfile/zip?File=" + std::to_string(SENSORID) + "/" ;
        ESP_LOGI(TAG, "Procesando %s%s", url.c_str(), f.c_str());
        process_file(url, f, refresh);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 4. Reiniciar servidor web
    ESP_LOGI(TAG, "Reiniciando servidor web...");
    start_webserver(global_dispatcher_ptr);

    ESP_LOGI(TAG, "Actualización web finalizada.");

    vTaskDelete(NULL);
}

// ======================================================
// 8. Clase UpdateWebCommand
// ======================================================
class UpdateWebCommand : public Command {
public:
    const char* name() const override { return "webupdate"; }
    const char* usage() const override { return "webupdate [Refresh=true]"; }
    int minArgs() const override { return 0; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override 
    {
        bool refresh = false;

        if (!args.empty()) {
            if (args[0] == "Refresh=true" || args[0] == "refresh=true") {
                refresh = true;
            }
        }

        xTaskCreate(
            update_web_task,
            "upd_web",
            8192,
            (void*)refresh,
            5,
            NULL
        );

        return "Actualización web iniciada en background";
    }
};

// ======================================================
// 9. Registrar comando en dispatcher. lo hacemos en cmd_set.cpp porque es un comando de utilidad, no de actualización, y queremos que aparezca en el help desde el principio. Si lo registramos en utils_update_webs.cpp no va a aparecer en el help porque el help solo lista los comandos registrados en el dispatcher al momento de su creación, y si el comando update_web se registra después del help, entonces no va a aparecer en la lista de comandos disponibles que muestra el help. Por eso lo registramos acá, para que esté disponible desde el principio y aparezca en el help.
// ======================================================
void register_update_web_command(CommandDispatcher& dispatcher)
{
    dispatcher.registerCommand(std::make_unique<UpdateWebCommand>());
}
