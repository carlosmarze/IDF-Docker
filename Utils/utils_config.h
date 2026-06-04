#ifndef UTILS_CONFIG_H
#define UTILS_CONFIG_H

#include "esp_err.h"
#include <string> 
#include <vector>
#include "utils_cmd_dispatcher.h" //para usar dispatcher

// --- DEFINES ---
#define MOUNT_POINT "/fs"
#define WIFI_JSON_FILE MOUNT_POINT "/wifi.json"
#define CONFIG_FILE_PATH MOUNT_POINT "/config.txt"
#define SSIDDEFAULT "ESPSSID"
#define PASSDEFAULT "esppassword"
#define WIFICONNECTRETRY 5
#define MODO_AP_SSID "MiTSESP-Setup"
#define MODO_AP_PASS "setup1234"
#define CMDWEBPREFIX "TSComm,"
#define MQTT_MAX_PAYLOAD 512
#define MQTT_BROKER_HOST "node02.myqtthub.com" 
#define MQTT_BROKER_USERNAME "MisESP8266-Dev"
#define MQTT_BROKER_PASSWORD "AnHPt32KDKb5WjDG"
#define MQTT_BROKER_PORT 1883 // O el puerto específico que uses (ej: 8080, 8883)

#define MAX_TOPIC_LENGTH 32 //"miTSESP/K/7001" con 16 andaría bien pero dejamos margen
#define MQTT_TOPIC_BASE "miTSESP/"
//#define MQTT_TOPICOK "miTSESP/K/"
//#define MQTT_TOPICA "miTSESP/A/"
//#define MQTT_SUBSCRIBE "miTSESP/Q/"


// --- VARIABLES Y FUNCIONES C++ (Fuera del extern "C") ---
extern std::string g_pending_ssid;
extern std::string g_pending_pass;
// indica si el WiFi está realmente operativo
//extern bool wifi_ready;
//extern bool g_manual_wifi_connect;
//extern bool g_ota_en_progreso; // Variable global OTA
bool cargar_config_desde_file(CommandDispatcher* disp); // Movida aquí fuera


#ifdef __cplusplus
extern "C" {
#endif

// --- ESTRUCTURA Y FUNCIONES C COMPATIBLES ---
typedef struct {
    char ssid[32];
    char password[64];
    char sensor_id_str[16];
    int sensor_id_int;         
    char api_endpoint[128]; 
    int api_port;           
    char write_key[16];     
    char read_key[16];      
    char channel_name[32];  
    uint32_t polling_interval_sec;
    bool verbose_logging;
} app_config_t;

// --- VARIABLES GLOBALES COMPARTIDAS ---
extern bool g_ota_en_progreso;
extern app_config_t g_app_config;

//esp_err_t load_sensor_config(void);
//int get_sensor_id(void);


#ifdef __cplusplus
}
#endif

#endif