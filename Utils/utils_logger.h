#ifndef UTILS_LOGGER_H
#define UTILS_LOGGER_H

#include "freertos/FreeRTOS.h"

// Definiciones de rutas y tamaños
#define MAIN_LOG      MOUNT_POINT "/log.txt"
#define MAX_LOG_SIZE  (10 * 1024) 
#define MAX_FILES     3 

// --- BLOQUE PARA C Y C++ ---
#ifdef __cplusplus
extern "C" {
#endif
// Cola global de logs de eventos wque no pueden logearse directamente (ej: eventos MQTT, WiFi, etc.) porque necesitan ser muy livianas para evitar stack overflow o deadlocks. Esta cola es leída por el LoggerTask para escribir en el sistema de archivos.
extern QueueHandle_t g_log_queue;

// Niveles de log
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

// Buffer compartido para formateo
#define LOG_BUFFER_SIZE 256
// Macros helper para facilitar el uso
#define LOGD(tag, format, ...) write_system_log_new(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOGI(tag, format, ...) write_system_log_new(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOGW(tag, format, ...) write_system_log_new(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOGE(tag, format, ...) write_system_log_new(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOGN(tag, format, ...) write_system_log_new(LOG_LEVEL_NOLOG, tag, format, ##__VA_ARGS__)

void write_system_log_new(log_level_t level, const char* tag, const char* format, ...) ;

// Códigos de evento para logging, revisar, lo de los evt, creo que no se usa más
typedef enum {
    LOG_EVT_NONE = 0,
    LOG_EVT_WIFI_DISCONNECTED,
    LOG_EVT_WIFI_DISCONNECTED_MANUAL,
    LOG_EVT_WIFI_GOT_IP,
    LOG_EVT_WIFI_LOST_IP,
    LOG_EVT_WIFI_AP_START,
    LOG_EVT_WIFI_AP_STACONNECTED,
    LOG_EVT_IP_LOST,
    LOG_EVT_MQTT_CONNECTED,
    LOG_EVT_MQTT_DISCONNECTED,
    LOG_EVT_MQTT_ERROR,
    LOG_EVT_MQTT_DATA,
    LOG_EVT_HTTP_ERROR,
    LOG_EVT_HTTP_FINISH,
    LOG_EVT_MAX
    // agregás más si querés
} log_event_t;
// Tabla estática de mensajes alineada con el enum
static const char* LOG_EVENT_MSG[LOG_EVT_MAX] = {
    [LOG_EVT_NONE]              = "Sin evento",
    [LOG_EVT_WIFI_DISCONNECTED] = "WiFi: STA desconectado",
    [LOG_EVT_WIFI_DISCONNECTED_MANUAL] = "WiFi: Desconexión voluntaria",
    [LOG_EVT_WIFI_GOT_IP]       = "WiFi: IP obtenida",
    [LOG_EVT_WIFI_LOST_IP]      = "WiFi: IP perdida",
    [LOG_EVT_WIFI_AP_START]     = "WiFi: AP iniciado",
    [LOG_EVT_WIFI_AP_STACONNECTED] = "WiFi: Cliente conectado al AP",
    [LOG_EVT_IP_LOST]           = "IP: Perdida de IP",
    [LOG_EVT_MQTT_CONNECTED]    = "MQTT: Conectado",
    [LOG_EVT_MQTT_DISCONNECTED] = "MQTT: Desconectado",
    [LOG_EVT_MQTT_ERROR]        = "MQTT: Error",
    [LOG_EVT_MQTT_DATA]         = "MQTT: Data recibida",
    [LOG_EVT_HTTP_ERROR]        = "HTTP: Error",
    [LOG_EVT_HTTP_FINISH]       = "HTTP: Transferencia finalizada"
};

// Tarea que procesa la cola y llama a write_system_log
void log_worker_task(void* arg);

// Estas funciones pueden ser llamadas desde archivos .c y .cpp
void write_system_log(const char* tag, const char* message);
void rotate_logs();
void clear_all_logs();
//void init_logger_mutex();
void init_logger_system();


#ifdef __cplusplus
}
#endif
// ----------------------------


// --- BLOQUE SOLO PARA C++ ---
#ifdef __cplusplus
#include <string> // Importante: el include de string solo aquí

// Esta sobrecarga (overload) SOLO es visible para archivos .cpp
void write_system_log(const char* tag, const std::string& message);

#endif
// ----------------------------

#endif // UTILS_LOGGER_H