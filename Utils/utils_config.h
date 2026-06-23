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
//#define MQTT_BROKER_HOST "node02.myqtthub.com" 
//#define MQTT_BROKER_USERNAME "MisESP8266-Dev"
//#define MQTT_BROKER_PASSWORD "AnHPt32KDKb5WjDG"
//#define MQTT_BROKER_PORT 1883 // O el puerto específico que uses (ej: 8080, 8883)
//#define MQTT_HIVEMQ_HOST "ce6972bb89804ca2a0522c31a4f3111d.s2.eu.hivemq.cloud" //51 caracteres
//#define MQTT_HIVEMQ_PORT 8883
//#define MQTT_HIVEMQ_USERNAME "MisESP32-Dev"
//#define MQTT_HIVEMQ_PASSWORD "AnHPt32KDKb5WjDG"
//#define MQTT_HIVEMQ_ROOT_CERT_PEM MOUNT_POINT "/certs/hivemq_ca.pem" // Ruta al certificado raíz para HiveMQ Cloud
#define MAX_TOPIC_LENGTH 32 //"miTSESP/K/7001" con 16 andaría bien pero dejamos margen
#define MQTT_TOPIC_BASE "miTSESP/"
//#define MQTTHIVE 1 // ahora es variable en config.txt Definir esta macro si queremos usar HiveMQ Cloud como broker MQTT, si no la definimos se usará el broker que tengas configurado en utils_mqtt.cpp (ej: Mosquitto local o en la nube)
//#define MQTT_TOPICOK "miTSESP/K/"
//#define MQTT_TOPICA "miTSESP/A/"
//#define MQTT_SUBSCRIBE "miTSESP/Q/"

//Variables de conexión con la WEB, para el update OTA y para bajar archivos de configuración, 
extern char urlUpdate[128]; //url para el update OTA
#define URL_BASE "carze.pythonanywhere.com" //url base
#define urlUpdateDef "https://carze.pythonanywhere.com/update" //url base para el update OTA, sin parámetros
#define URL_FILES_UPDATE "https://carze.pythonanywhere.com/field/html" //url para bajar archivos de configuración. Ejemplo: https://carze.pythonanywhere.com/field/html?api_key=REZAQ4BH81OQP9PZ&SensorID=25
//url para bajar archivos de configuración. Ejemplo: https://carze.pythonanywhere.com/field/html?api_key=REZAQ4BH81OQP9PZ&SensorID=25


// --- VARIABLES Y FUNCIONES C++ (Fuera del extern "C") ---
extern std::string g_pending_ssid;
extern std::string g_pending_pass;
// indica si el WiFi está realmente operativo
//extern bool wifi_ready;
//extern bool g_manual_wifi_connect;
//extern bool g_ota_en_progreso; // Variable global OTA
extern int SensorID; // Identificador único del sensor lo definiremos en main.cpp o donde le pongamos el valor (leido de file seguramente)
extern bool MqttTLS;
extern char esquema[]; // Esquema de datos para miTS, lo definimos como constante porque no cambia, pero podría ser una variable si se quisiera usar el mismo firmware para distintos esquemas.
extern char WEB_USER[];
extern char WEB_PASS[];
extern char mqtthost[];
extern char mqttuser[];
extern char mqttpass[];
extern char mqtttopicbase[];
extern char mqttcert[];
extern uint32_t mqttport;
bool cargar_config_desde_file(CommandDispatcher* disp); // Movida aquí fuera
bool cargar_config_desde_file_directo() ; // Carga directa sin pasar por el dispatcher, para usar en app_main antes de iniciar el dispatcher
extern bool first_run_done; // Declaramos esta variable externa para controlar la primera ejecución, está en config.cpp
extern bool autowebupdate; // Declaramos esta variable externa para controlar si se hace autoupdate de archivos web al iniciar el sistema, está en config.cpp
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