#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "utils_logger.h"
#include "utils_files.h"
#include "utils_time.h" // Para funciones de fecha y hora

// Usamos tu definición de punto de montaje
//#define MOUNT_POINT "/fs"
//Crear mutex global para proteger el acceso a los logs y evitar que se borren cuando por error al rotar logs
//Arrancarlo desde main.cpp con init_logger_mutex() antes de cualquier llamada a write_system_log() o clear_all_logs()          
static SemaphoreHandle_t log_mutex = nullptr;
//Crear logger asincrónica con cola para eventos. Se inicializan desde init_logger
QueueHandle_t g_log_queue = nullptr; //Para la cola de mensajes a loggear desde otros archivos (ej: MQTT, WiFi, etc.) que no pueden usar write_system_log directamente por temas de stack o deadlocks. El LoggerTask leerá esta cola y llamará a write_system_log con los mensajes recibidos.
static const char* TAG = "LOG_EV";
void log_worker_task(void* arg)
{
    log_event_t evt;

    write_system_log(TAG, "\n\nLog worker iniciado");

    while (true) {
        if (xQueueReceive(g_log_queue, &evt, portMAX_DELAY) == pdTRUE) {

            if (evt > 0 && evt < LOG_EVT_MAX) {
                const char* msg = LOG_EVENT_MSG[evt];
                write_system_log(TAG, msg);
                ESP_LOGI(TAG, "%s", msg);
            } else {
                //write_system_log(TAG, "Evento de log %d inválido", evt);
                ESP_LOGI(TAG, "Evento %d inválido", evt);
            }
        }
    }
}


void init_logger_system()
{
    // Mutex para proteger write_system_log
    if (!log_mutex)
        log_mutex = xSemaphoreCreateMutex();

    // Cola para eventos de log
    if (!g_log_queue)
        g_log_queue = xQueueCreate(32, sizeof(log_event_t));

    // Tarea que procesa la cola
    static bool log_task_started = false;
    if (!log_task_started) {
        xTaskCreate(log_worker_task, "log_worker", 4096, NULL, 4, NULL);
        log_task_started = true;
    }
}

// Función para inicializar el mutex del logger, debe ser llamada desde main.cpp antes de cualquier operación de logging
//void init_logger_mutex() {
//  log_mutex = xSemaphoreCreateMutex();
//}

static const char *L_TAG = "LOGGER";
// Lógica de rotación: log2 -> log3, log1 -> log2, log.txt -> log1
void rotate_logs() {
    char old_n[64], new_n[64];
    
    ESP_LOGW(L_TAG, "Iniciando rotación de logs...");

    // 1. El último archivo (log3) simplemente se borra
    snprintf(old_n, sizeof(old_n), "%s/log%d.txt", MOUNT_POINT, MAX_FILES);
    unlink(old_n); 

    // 2. Desplazar archivos intermedios (2->3, 1->2)
    for (int i = MAX_FILES - 1; i >= 1; i--) {
        snprintf(old_n, sizeof(old_n), "%s/log%d.txt", MOUNT_POINT, i);
        snprintf(new_n, sizeof(new_n), "%s/log%d.txt", MOUNT_POINT, i + 1);
        
        // IMPORTANTE: En algunos FS, rename falla si new_n existe
        unlink(new_n); 
        if (rename(old_n, new_n) == 0) {
            ESP_LOGI(L_TAG, "Movido %s a %s", old_n, new_n);
        }
    }

    // 3. Mover el principal log.txt -> log1.txt
    snprintf(new_n, sizeof(new_n), "%s/log1.txt", MOUNT_POINT);
    unlink(new_n); // Limpiar destino
    if (rename(MAIN_LOG, new_n) == 0) {
        ESP_LOGI(L_TAG, "Archivo principal rotado a log1.txt");
    } else {
        ESP_LOGE(L_TAG, "Error crítico al rotar log principal!");
    }
}

void write_system_log(const char* tag, const char* message) {
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    struct stat st;
    // 1. Control de rotación
    if (stat(MAIN_LOG, &st) == 0 && st.st_size >= MAX_LOG_SIZE) {
        rotate_logs();
    }

    // 2. Abrir en modo append
    FILE* f = fopen(MAIN_LOG, "a");
    if (f == NULL) return;

    // 3. Timestamp inteligente
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char timestamp[32];
    // Si el año es menor a 2020, usamos el tiempo desde el arranque (uptime)
    if (timeinfo.tm_year < (2020 - 1900)) { 
        // esp_timer_get_time() devuelve microsegundos desde el boot, es más preciso
        snprintf(timestamp, sizeof(timestamp), "UP:%llds", esp_timer_get_time() / 1000000);
    } else {
        strftime(timestamp, sizeof(timestamp), "%d/%m %H:%M:%S", &timeinfo);
    }

    // 4. Escritura
    fprintf(f, "[%s] [%s] %s\n", timestamp, tag, message);

    // 5. BLINDAJE: Forzar que los datos salgan de la RAM a la Flash
    fflush(f); 
    // fsync(fileno(f)); // Opcional: asegura la integridad física del sistema de archivos
    
    fclose(f);
    xSemaphoreGive(log_mutex);
}
void write_system_log(const char* tag, const std::string& message) { //override para std::string
    write_system_log(tag, message.c_str());
}

void clear_all_logs() {
    xSemaphoreTake(log_mutex, portMAX_DELAY);
    char filename[64];
    
    // Borrar el principal
    unlink(MAIN_LOG);
    
    // Borrar los rotados (log1, log2, log3)
    for (int i = 1; i <= MAX_FILES; i++) {
        snprintf(filename, sizeof(filename), "%s/log%d.txt", MOUNT_POINT, i);
        unlink(filename);
    }
    ESP_LOGI(L_TAG, "Todos los logs han sido eliminados.");
}