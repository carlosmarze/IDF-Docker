#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h" // Para esp_timer_get_time()
#include <time.h>      // Para las funciones de tiempo (strftime, etc.)
#include <dirent.h>      // Para opendir, readdir, closedir
#include <sys/stat.h>    // Para stat
#include <cstdio>
#include <string>
#include <vector>
#include <ctype.h> // Para isdigit
 #include "esp_littlefs.h"  // Incluye la biblioteca

#include "utils_cmd_set.h"
#include "utils_cmd_dispatcher.h" //para usar CommandDispatcher
#include "utils_cmd_processor.h"
#include "utils_cmd_webupdate.h"
#include "utils_cmd_parser.h"
#include "utils_config.h"
#include "utils_ota_worker.h"
#include "utils_webs.h"
#include "utils_logger.h"
#include "utils_config.h" 
#include "utils_wifi.h" //para save_wifi_network
#include "utils_mqtt.h"
#include "utils_bt.h"

#include "MisVariablesProyecto.h"

static const char *TAG = "CMDSET";
//estas funciones setean los parametros de configuracion y se registran en el dispatcher de comandos

// Asumimos que tienes estas variables globales donde vive la config actual
//std::string g_pending_ssid = "";
//std::string g_pending_pass = "";
class SetSensorID : public Command {
public:
    const char* name() const override { return "setsensorid"; }
    const char* usage() const override { return "<sensorid> - Configura el ID del sensor"; }
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        SensorID = std::stoi(args[0]);
        // Construir línea de configuración
        std::string cfg = "setsensorid=" + std::to_string(SensorID);
        save_config(cfg.c_str());

        std::string respuesta = "SensorID seteado a " + std::to_string(SensorID);
        //write_system_log("CONFIG", respuesta.c_str());
        ESP_LOGI(TAG, "%s",respuesta.c_str());
        return "OK: Sensor ID seteado.";
    }
};


class SetSSID : public Command {
public:
    const char* name() const override { return "setssid"; }
    const char* usage() const override { return "[ssid] - Configura el nombre de la red WiFi"; }
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        g_pending_ssid = args[0];
        
        // Si ya tenemos ambos (SSID y Pass), intentamos guardar en el historial
        if (!g_pending_pass.empty()) {
            save_wifi_network(g_pending_ssid.c_str(), g_pending_pass.c_str());
        }

        ESP_LOGI(TAG, "SSID preparado: %s", g_pending_ssid.c_str());
        return "OK: SSID seteado. Use 'connect' para aplicar o 'setwifipass' si falta la clave.";
    }
};

class SetWifiPass : public Command {
public:
    const char* name() const override { return "setwifipass"; }
    const char* usage() const override { return "[password] - Configura la clave del WiFi"; }
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        g_pending_pass = args[0];

        // Si ya tenemos el SSID, guardamos en el JSON para que sea persistente
        if (!g_pending_ssid.empty()) {
            save_wifi_network(g_pending_ssid.c_str(), g_pending_pass.c_str());
            ESP_LOGI(TAG, "Red guardada en historial JSON.");
        }

        ESP_LOGI(TAG, "Password preparada.");
        return "OK: Password seteada y guardada en historial.";
    }
};



class SetWifi : public Command {

public:
    const char* name() const override { return "setwifi"; }
    const char* usage() const override { return "setwifi [ssid] [password] - Configura SSID y clave del WiFi"; }
    int minArgs() const override { return 2; }
    bool positionalArgs() const override { return true; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        ESP_LOGI(TAG,"Comando setwifi args0=%s, args1=%s", args[0].c_str(), args[1].c_str());
        save_wifi_network(args[0].c_str(), args[1].c_str());

        return "WiFi guardado correctamente. Reboot para conectar";
    }
};


class SetMiTSServer : public Command {
public: 
    const char* name() const override { return "setmitsserver"; }
    const char* usage() const override { return "[server] - Setea la variable mitsServer"; } // <-- Añadir
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        std::string tsServer = args[0];
        ESP_LOGI(TAG, "Configurando MiTSServer: %s", tsServer.c_str());
        // Aquí llamas a tu lógica de NVS o WiFi
        return "OK: MiTSServer cambiado";
    }
};

class CmdReboot : public Command {
public:
    // Firmas corregidas con 'const' y 'override'
    const char* name() const override { return "reboot"; }
    const char* usage() const override { return "[mins] - Reinicia el ESP32 (inmediato o programado)"; } // <-- Añadir
    int minArgs() const override { return 0; }

    // Callback que se ejecutará cuando el tiempo expire
    static void reboot_timer_callback(void* arg) {
        ESP_LOGW("REBOOT", "Tiempo cumplido. Reiniciando ahora...");
        esp_restart();
    }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        int delay_mins = 0;
        if (!args.empty()) {
            delay_mins = atoi(args[0].c_str()); 
        }

        if (delay_mins <= 0) {
            ESP_LOGW("CMD", "Reinicio inmediato solicitado.");
            esp_restart();
        } else {
            ESP_LOGI("CMD", "Programando reinicio en %d minutos...", delay_mins);

            // Configuración del Timer
            //  Declarar y luego igualar a cero el resto
            esp_timer_create_args_t timer_args = {}; // Inicializa todo en 0/nullptr
            timer_args.callback = &reboot_timer_callback;
            timer_args.name = "one-shot-reboot";
            esp_timer_handle_t timer_handle;
            esp_timer_create(&timer_args, &timer_handle);
            
            // Convertir minutos a microsegundos: mins * 60s * 1.000.000us
            uint64_t timeout_us = (uint64_t)delay_mins * 60 * 1000000;
            esp_timer_start_once(timer_handle, timeout_us); //esto no bloquea al dispatcher el tiempo que dura el timer

            ESP_LOGI("CMD", "Dispatcher libre. Puedes seguir enviando comandos.");
        }
        return "OK: Reinicio programado en " + std::to_string(delay_mins) + " minutos.";
    }
};

class StartBLE : public Command {
public:
    const char* name() const override { return "ble"; }
    const char* usage() const override { return "<on/off> - Arranca BT. off=hace reboot"; }
    int minArgs() const override { return 1; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        const std::string &mode = args[0];
        bool on = (strcasecmp(mode.c_str(), "on") == 0);
        if(on){
            if(ble_status()) {
                return "BLE ya está activo";
            }
            else {
                std::string msg = "arrancando BLE";
                LOGI(TAG, msg.c_str());
                ble_uart_init();
                return msg;
            }
        }
        else {
            LOGI(TAG, "\nReboot para apagar BLE\n");
            esp_restart();
        }
    }
    
   
};

class LedCommandB : public Command {
public:
    const char* name() const override { return "set_led"; }
    const char* usage() const override { return "[nroLed=On/Off] - TBI Setea el estado del LED nroLed"; } // <-- Añadir
    int minArgs() const override { return 2; }
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
        const std::string &mode = args[0];
        int gpio = std::stoi(args[1]);
        bool on = (strcasecmp(mode.c_str(), "on") == 0);
        ESP_LOGI(TAG, "LED gpio=%d -> %s", gpio, on ? "ON" : "OFF");
        return "LED set";
    }
};




// -----------------------------------------------------------------------------
// Implementaciones de comandos
// -----------------------------------------------------------------------------

class HelpCommand : public Command {
private:
    const CommandDispatcher& _dispatcher; // Referencia al motor de comandos

public:
    // Constructor: recibe el dispatcher por referencia
    HelpCommand(const CommandDispatcher& disp) : _dispatcher(disp) {}

    const char* name() const override { return "help"; }
    const char* usage() const override { return "- Muestra esta lista de comandos."; }
    int minArgs() const override { return 0; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        std::string output = "" ;
        output += "========================================";
        output += "   LISTA DE COMANDOS DISPONIBLES";
        output += "========================================\n";
        
        // Recorremos el mapa del dispatcher
        const auto& cmdMap = _dispatcher.getCommands();
        
        for (auto const& [name, cmd] : cmdMap) {
            // Imprimimos Nombre (alineado a 10 espacios) y su uso
            //ESP_LOGI("HELP", " %-10s : %s", cmd->name(), cmd->usage());
            output +=  std::string(cmd->name()) + " : " + std::string(cmd->usage());
            output += "\n";
        }

        ESP_LOGI(TAG, "%s", output.c_str());
        return output;
    }
};




class connectWifi : public Command {
public:
    const char* name() const override { return "connectwifi"; }

    const char* usage() const override {
        return "connectwifi - Conecta a la última red WiFi conocida o guardada";
    }

    int minArgs() const override { return 0; }

    bool positionalArgs() const override { return false; }
    /* a veces este genera stack overflow, por eso lo vamos a correr en una task aparte wifi_connect_task y desde el comando solo notificamos para que arranque 
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {

        iniciar_proceso_conexion_maestra();

        return "Iniciando conexión WiFi...";
    }*/
    std::string execute(cmd_source_t, const std::vector<std::string>&) {
        xTaskNotifyGive(wifi_connect_task_handle);
        g_manual_wifi_connect = true; // Indicamos que se solicitó una conexión manual para que el sistema no intente reconectar automáticamente
        return "Solicitando conexión WiFi async.";
    }
};


class delSsid : public Command {
public:
    const char* name() const override { return "delssid"; }

    const char* usage() const override {
        return "delssid <ssid> - Borra una red WiFi de la lista de redes guardadas";
    }

    int minArgs() const override { return 1; }

    bool positionalArgs() const override { return false; }

    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {

        const std::string& ssid = args[0];

        bool ok = delete_wifi_network(ssid.c_str());

        if (ok) {
            return "Red WiFi eliminada correctamente: " + ssid;
        } else {
            return "No se encontró la red WiFi: " + ssid;
        }
    }
};


class OtaCommand : public Command {
public:
    const char* name() const override { return "ota"; }
    const char* usage() const override { return "[OTA Server],chunksize=<size>,reboot=<yes/no>,https=<yes/no>  "; } // <-- Añadir
    int minArgs() const override { return 1; } // al menos un bloque de argumentos

    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
        if (args.empty()) {
            ESP_LOGW(TAG, "OTA requiere parámetros");
            return "OTA requiere parámetros"    ;
        }

        // args[0] contiene todo el bloque: "server=http://..., chunksize=2048, reboot=no"
        //printf("OTA Argumento completo: %s\n", args[0].c_str());
        auto kvs = parse_lineX(args[0]);  
        //auto kvs = parse_line(args[0], ',', '='); // separador entre parámetros = coma, separador nombre/valor = '='

        std::string url;
        int chunk = 0;
        bool reboot = true;
        bool httpspar = false;

        for (auto &kv : kvs) {
            if (kv.name == "server") {
                url = kv.arg;
            } else if (kv.name == "chunksize") {
                chunk = atoi(kv.arg.c_str());
            } else if (kv.name == "reboot") {
                reboot = (kv.arg != "no");
            
            } else if (kv.name == "https") {
                httpspar = (kv.arg != "no"); // este es un parámetro bool, que sale false si es "no" y true cualquier otra cosa
            }
        }

        // VALIDACIÓN CRÍTICA: Evitar crash si URL está vacía
        if (url.empty()) {
            ESP_LOGE(TAG, "OTA: URL vacía. Args recibidos: %s", args[0].c_str());
            return "Error OTA: URL del servidor no especificada";
        }

        // Validar que la URL empiece con http:// o https://
        if (url.find("http://") != 0 && url.find("https://") != 0) {
            ESP_LOGE(TAG, "OTA: URL inválida: %s", url.c_str());
            return "Error OTA: URL debe empezar con http:// o https://";
        }

        ESP_LOGI(TAG, "OTA job: URL='%s' chunk=%d reboot=%d https=%d", url.c_str(), chunk, reboot, httpspar);
        
        ota_job_t job = {};
        snprintf(job.url, sizeof(job.url), "%s", url.c_str());
        job.reboot_after = reboot;
        job.chunk_size = chunk;
        job.https = httpspar;


        // acá disparás tu rutina OTA con esos parámetros
        // return ota_worker_start(url, chunk, reboot);
        return ota_submit(&job) ? "Ota Ok" : "Ota Error";
    }
};
class dnlFile : public Command { //Baja desde la web un archivo cualquiera
public:
    const char* name() const override { return "dnlFile"; } //dnldFile server=http://...,file=nombrearchivo
    int minArgs() const override { return 1; } //Si no se elige nombre de archivo, se toma el que viene en la URL
    const char* usage() const override { return "[server=http://...] [file=nombrearchivo] - Descarga un archivo desde un servidor HTTP"; } // <-- Añadir
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
        if (args.empty()) {
            ESP_LOGW(TAG, "dnlFile requiere parámetros");
            return "dnlFile requiere parámetros";
        }
        // args[0] contiene todo el bloque: "server=http://..., file=nombrearchivo"
        //implementar
        //auto kvs = parse_line(args[0], ',', '=');  // separador entre parámetros = coma, separador nombre/valor = '='    
        auto kvs = parse_lineX(args[0]);  //
        return "bajando archivo de server=" + args[0] + ", File=" + args[1];
    }
};



#include "esp_system.h"
#include "esp_timer.h"

class CmdStatus : public Command {
public:
    const char* name() const override { return "status"; }
    const char* usage() const override { return "- Muestra el estado de salud, WiFi y tiempo del sistema."; }
    int minArgs() const override { return 0; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        // 1. Uptime y Memoria
        int64_t uptime_us = esp_timer_get_time();
        int seconds = (int)(uptime_us / 1000000);
        size_t free_heap = esp_get_free_heap_size();

        // 2. Info de WiFi
        wifi_ap_record_t ap_info;
        static char wifi_info[128];
        strcpy(wifi_info, "Desconectado");
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            esp_netif_ip_info_t ip_info;
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_get_ip_info(netif, &ip_info);
            
            snprintf(wifi_info, sizeof(wifi_info), "STA: %s | RSSI: %d dBm | IP: " IPSTR, 
                    ap_info.ssid, ap_info.rssi, IP2STR(&ip_info.ip));
        } else {
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
                snprintf(wifi_info, sizeof(wifi_info), "Modo AP Activo (Rescate)");
            }
        }

        // 3. Info de Tiempo (NTP)
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        static char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%d/%m/%Y %H:%M:%S", &timeinfo);

        // 4. Info MQTT
        static char mqtt_info[256];
        if (!(mqtt_is_initialized())) {
            snprintf(mqtt_info, sizeof(mqtt_info), "MQTT no inicializado");
        } else if (!mqttconnStatus) {
            snprintf(mqtt_info, sizeof(mqtt_info), "MQTT desconectado | TLS: %s", 
                    MqttTLS ? "ON" : "OFF");
        } else {
            static char topic[128];
            generar_topico_mqtt("Q", SensorID, topic, sizeof(topic));

            snprintf(mqtt_info, sizeof(mqtt_info),
                    "MQTT conectado | TLS: %s | Suscripto a: %s", 
                    MqttTLS ? "ON" : "OFF", topic);
        }

        // 5. Info LittleFS
        static char fs_info[128];
        size_t total_bytes = 0, used_bytes = 0;
        esp_err_t fs_ret = esp_littlefs_info("storage", &total_bytes, &used_bytes);
        
        if (fs_ret == ESP_OK) {
            size_t free_bytes = total_bytes - used_bytes;
            float used_percent = (total_bytes > 0) ? (used_bytes * 100.0f / total_bytes) : 0.0f;
            
            snprintf(fs_info, sizeof(fs_info), 
                    "Total: %u KB | Usado: %u KB (%.1f%%) | Libre: %u KB",
                    total_bytes / 1024, used_bytes / 1024, used_percent, free_bytes / 1024);
        } else {
            snprintf(fs_info, sizeof(fs_info), "Error al obtener info: %s", esp_err_to_name(fs_ret));
        }

        // 6. Construir respuesta final
        static char output[1024]; // Aumentado de 768 a 1024 para acomodar la nueva info
        int pos = 0;
        pos += snprintf(output + pos, sizeof(output) - pos, "\n--- SYSTEM STATUS ---\n");
        pos += snprintf(output + pos, sizeof(output) - pos, "  Version   : %s\n", version_info);
        pos += snprintf(output + pos, sizeof(output) - pos, "  Sensor    : %s\n", SensorID > 0 ? std::to_string(SensorID).c_str() : "N/A");
        pos += snprintf(output + pos, sizeof(output) - pos, "  Time      : %s\n", strftime_buf);
        pos += snprintf(output + pos, sizeof(output) - pos, "  Uptime    : %d d, %02d:%02d:%02d\n", 
                        seconds / 86400, (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
        pos += snprintf(output + pos, sizeof(output) - pos, "  WiFi      : %s\n", wifi_info);
        pos += snprintf(output + pos, sizeof(output) - pos, "  MQTT      : %s\n", mqtt_info);
        pos += snprintf(output + pos, sizeof(output) - pos, "  LittleFS  : %s\n", fs_info);
        pos += snprintf(output + pos, sizeof(output) - pos, "  Free Heap : %u KB\n", free_heap / 1024);
        pos += snprintf(output + pos, sizeof(output) - pos, "  Reset Rsn : %d\n", (int)esp_reset_reason());
        pos += snprintf(output + pos, sizeof(output) - pos, "---------------------\n");

        return std::string(output);
    }

};
class ClearLogsCommand : public Command {
public: 
    // Usamos 'name' y 'usage' para coincidir con tu interfaz Command
    const char* name() const override { return "clear_logs"; }
    const char* usage() const override { return "- Elimina todos los archivos de log de la memoria Flash."; }
    int minArgs() const override { return 0; } // No requiere argumentos

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        // Llamamos a la función de limpieza que definimos antes
        clear_all_logs();
        
        // Opcional: registrar que se limpió
        LOGI("SYSTEM", "Logs reseteados por el usuario.");
        
        return "OK: Todos los archivos de log han sido eliminados.";
    }
};


#include "cJSON.h"
#include "esp_wifi.h"

class WifiScanCommand : public Command {
public:
    // Debe devolver const char* y ser const
    const char* name() const override { return "scan"; }
    const char* usage() const override { return "Escanea redes WiFi y devuelve un JSON"; }
    int minArgs() const override { return 0; } // Implementamos minArgs que faltaba

    // Debe incluir cmd_source_t src aunque no lo uses
    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) != ESP_OK) {
            return "{\"error\":\"WiFi no inicializado\"}";
        }

        wifi_scan_config_t scan_config = {};
        scan_config.show_hidden = false;
        scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;

        if (esp_wifi_scan_start(&scan_config, true) != ESP_OK) {
            return "{\"error\":\"Fallo al iniciar escaneo\"}";
        }

        uint16_t number = 15;
        wifi_ap_record_t ap_info[15];
        esp_wifi_scan_get_ap_records(&number, ap_info);

        cJSON *root = cJSON_CreateArray();
        for (int i = 0; i < number; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "ssid", (char*)ap_info[i].ssid);
            cJSON_AddNumberToObject(item, "rssi", ap_info[i].rssi);
            cJSON_AddNumberToObject(item, "channel", ap_info[i].primary);
            cJSON_AddItemToArray(root, item);
        }

        char *json_str = cJSON_PrintUnformatted(root);
        std::string result(json_str);
        cJSON_Delete(root);
        cJSON_free(json_str);
        
        return result; 
    }
};
class WifiConnectCommand : public Command {
public:
    const char* name() const override { return "wificonnect"; }
    const char* usage() const override { return "wificonnect <ssid> <pass> - Conecta a una red dada"; }
    int minArgs() const override { return 2; } 

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        // Usamos la función que ya arreglamos antes (conectar_wifi)
        // Asegúrate de que utils_wifi.h esté incluido para que encuentre esta función
        if (conectar_wifi(args[0].c_str(), args[1].c_str(), 15000)) {
            return "OK: Conectado a SSID: " + args[0];
        } else {
            return "Error: No se pudo conectar a SSID: " + std::string(args[0]);
        }
    }
};


/************************************************
 * Comando dir o ls - lista archivos en memoria 
 *************************************************/

class DirCommand : public Command {
public:
    const char* name() const override { return "dir"; }
    const char* usage() const override { return "Muestra el contenido del directorio actual"; }
    int minArgs() const override { return 0; }  
    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        struct stat st;
        struct dirent *de;
        std::string response = "\nNombre\t\tTamaño\tFecha Modificación\n";

        // Si args está vacío, usamos "/"
        std::string folder = (args.size() > 0) ? args[0] : "";
        std::string full_path = MOUNT_POINT + folder;
        
        DIR *dr = opendir(full_path.c_str());
        if (dr == NULL) {
            return "No se pudo abrir el directorio: " + full_path;
        } 

        while ((de = readdir(dr)) != NULL) {
            std::string filename = de->d_name;
            if (filename == "." || filename == "..") continue;

            // --- EL CAMBIO CLAVE AQUÍ ---
            // Construimos la ruta completa al ARCHIVO: "/sdcard/folder/archivo.txt"
            std::string file_path = full_path;
            if (file_path.back() != '/') file_path += "/";
            file_path += filename;

            // Llamamos a stat sobre el archivo específico
            if (stat(file_path.c_str(), &st) == 0) {
                response += filename + "\t";
                if (filename.length() < 8) response += "\t"; // Ajuste de tabulación visual

                // 2. Tamaño
                if (st.st_size >= 1024) {
                    response += std::to_string(st.st_size / 1024) + " KB";
                } else {
                    response += std::to_string(st.st_size) + " B";
                }
                response += "\t";

                // 3. Fecha y Hora
                char date_buf[25];
                struct tm *tm_info = localtime(&st.st_mtime);
                strftime(date_buf, sizeof(date_buf), "%d/%m/%Y %H:%M:%S", tm_info);
                response += std::string(date_buf) + "\n";
                
            } else {
                response += filename + "\tError al leer metadatos\n";
            }
        }
        closedir(dr);
        return response;
    }
};



class MoreCommand : public Command {
public:
    const char* name() const override { return "more"; }
    const char* usage() const override { return "more <file> [-bytes] (max 1024b). Ej: more archivo.txt -100 muestra los ultimos 100 bytes"; }
    int minArgs() const override { return 1; }
    // 🔥 ESTE COMANDO ES POSICIONAL, la siguiente línea lo indica
    bool positionalArgs() const override { return true; }

    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        std::string path_str = MOUNT_POINT;
        path_str += args[0];
        
        long bytes_a_leer = 1024;
        bool modo_tail = false;

        if (args.size() > 1 && args[1][0] == '-') {
            if (args[1].length() > 1 && isdigit(args[1][1])) {
                bytes_a_leer = std::strtol(args[1].c_str() + 1, nullptr, 10);
                modo_tail = true;
            }
        }

        if (bytes_a_leer > 1024) bytes_a_leer = 1024;
        if (bytes_a_leer <= 0) bytes_a_leer = 1024;

        FILE* f = fopen(path_str.c_str(), "r");
        if (f == NULL) return "\nError: Archivo no encontrado";

        fseek(f, 0, SEEK_END);
        long tamano_total = ftell(f);
        
        if (modo_tail) {
            long offset = (tamano_total > bytes_a_leer) ? (tamano_total - bytes_a_leer) : 0;
            fseek(f, offset, SEEK_SET);
        } else {
            fseek(f, 0, SEEK_SET);
        }

        // --- SOLUCIÓN AL STACK OVERFLOW ---
        // Reservamos memoria en el HEAP, no en el STACK
        char* buffer = (char*)malloc(bytes_a_leer + 1);
        if (buffer == nullptr) {
            fclose(f);
            return "\nError: Memoria insuficiente para procesar el comando";
        }

        size_t n = fread(buffer, 1, bytes_a_leer, f);
        buffer[n] = '\0'; 
        fclose(f);

        std::string respuesta = "\n";
        respuesta += buffer;
        
        // Liberamos la memoria del heap inmediatamente después de usarla
        free(buffer);

        // Feedback informativo
        respuesta += "\n\n--- ";
        respuesta += (modo_tail ? "Ultimos " : "Primeros ") + std::to_string(n) + " bytes";
        if (tamano_total > (long)n && !modo_tail) {
            respuesta += " (Total: " + std::to_string(tamano_total) + " bytes)";
        }
        respuesta += " ---";

        return respuesta;
    }
};




/************************************************
 * Registra los comandos de utilidades
 *************************************************/

void register_utils_commands(CommandDispatcher& dispatcher) {
    // Le pasamos la propia 'dispatcher' al comando Help
    //dispatcher.registerCommand(std::make_unique<HelpCommand>(dispatcher));
    // Le pasamos la referencia 'dispatcher' al constructor de HelpCommand
    dispatcher.registerCommand(std::make_unique<HelpCommand>(dispatcher)); //necesita dispatcher para listar los comandos disponibles

    dispatcher.registerCommand(std::make_unique<CmdReboot>());
    dispatcher.registerCommand(std::make_unique<StartBLE>());
    dispatcher.registerCommand(std::make_unique<SetMiTSServer>());
    //dispatcher.registerCommand(std::make_unique<CmdFreeHeap>());
    //dispatcher.registerCommand(std::make_unique<HelpCommand>());
    //dispatcher.registerCommand(std::make_unique<LedCommand>());
    
    dispatcher.registerCommand(std::make_unique<OtaCommand>());
    dispatcher.registerCommand(std::make_unique<SetSensorID>());
    dispatcher.registerCommand(std::make_unique<SetSSID>());
    dispatcher.registerCommand(std::make_unique<SetWifiPass>());
    dispatcher.registerCommand(std::make_unique<SetWifi>()); 
    dispatcher.registerCommand(std::make_unique<WifiScanCommand>());
    dispatcher.registerCommand(std::make_unique<WifiConnectCommand>());
    dispatcher.registerCommand(std::make_unique<connectWifi>());
    dispatcher.registerCommand(std::make_unique<delSsid>());
    dispatcher.registerCommand(std::make_unique<CmdStatus>());

    dispatcher.registerCommand(std::make_unique<dnlFile>());
    dispatcher.registerCommand(std::make_unique<ClearLogsCommand>());

    dispatcher.registerCommand(std::make_unique<DirCommand>());
    dispatcher.registerCommand(std::make_unique<MoreCommand>());
    //dispatcher.registerCommand(std::make_unique<UpdateWebCommand>()); //comando webupdate está en utils_cmd_webupdate.cpp, pero lo registramos acá porque es un comando de utilidad, no de actualización. Si lo registramos en utils_update_webs.cpp no va a aparecer en el help porque el help solo lista los comandos registrados en el dispatcher al momento de su creación, y si el comando update_web se registra después del help, entonces no va a aparecer en la lista de comandos disponibles que muestra el help. Por eso lo registramos acá, para que esté disponible desde el principio y aparezca en el help.
    register_update_web_command(dispatcher); //comando webupdate está en utils_cmd_webupdate.cpp

}
/************************************************
 * Graba un comando de configuración en el archivo de configuración, reemplazando la línea si ya existe o agregándola al final si no existe. El formato del comando debe ser "nombre=valor". Por ejemplo: "ssid=MiRedWiFi". El archivo de configuración se define en CONFIG_FILE_PATH. Esta función se llama desde los comandos setssid y setwifipass para guardar la configuración WiFi de forma persistente.
 *************************************************/
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
extern bool first_run_done; // Declaramos esta variable externa para controlar la primera ejecución, está en config.cpp

void save_config(const char* new_cmd)
{

    const char* path = CONFIG_FILE_PATH;

    std::ifstream infile(path);
    std::vector<std::string> lines;
    lines.reserve(16);   // evita realocaciones

    std::string new_line(new_cmd);

    // Extraer el nombre del comando (antes del '=')
    std::string cmd_name;
    {
        size_t pos = new_line.find('=');
        if (pos != std::string::npos)
            cmd_name = new_line.substr(0, pos);
        else
            cmd_name = new_line; // caso raro
    }

    bool replaced = false;

    if (infile.good()) {
        std::string line;
        while (std::getline(infile, line)) {

            // Limpieza de espacios
            std::string trimmed = line;
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

            if (trimmed.rfind(cmd_name + "=", 0) == 0) {
                // Esta línea es el comando a reemplazar
                lines.push_back(new_line);
                replaced = true;
            } else {
                // Mantener línea original
                lines.push_back(line);
            }
        }
        infile.close();
    }

    // Si no estaba, agregarlo al final
    if (!replaced) {
        lines.push_back(new_line);
    }

    // Guardar archivo completo
    std::ofstream outfile(path, std::ios::trunc);
    for (auto& l : lines) {
        outfile << l << "\n";
    }
    outfile.close();
    //std::string log_msg = "save_config(): " + std::string(replaced ? "actualizado" : "agregado") + " '" + new_cmd + "' en " + path;
    LOGI(TAG,
         "save_config(): %s '%s' en %s",
         replaced ? "actualizado" : "agregado",
         new_cmd,
         path);    
    // write_system_log("CONFIG", log_msg.c_str());
    // ESP_LOGI("CONFIG", "%s", log_msg.c_str());
}
