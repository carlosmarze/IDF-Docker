#include "utils_ntp.h"
#include "utils_wifi.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <time.h>
#include <sys/time.h>
#include "utils_logger.h" // Para logging

static const char *TAG = "NTP_CLIENT";

// --- Variables Globales ---
static ntp_sync_status_t g_ntp_status = NTP_STATUS_UNINITIALIZED;
static SemaphoreHandle_t g_ntp_status_mutex = NULL;
static uint32_t g_sync_interval_s = 60; // Valor por defecto seguro
static const char *g_ntp_server1 = "pool.ntp.org";

// ... (Funciones set_ntp_status y get_ntp_status se mantienen igual) ...
static void set_ntp_status(ntp_sync_status_t status) {
    if (g_ntp_status_mutex && xSemaphoreTake(g_ntp_status_mutex, portMAX_DELAY) == pdTRUE) {
        g_ntp_status = status;
        xSemaphoreGive(g_ntp_status_mutex);
    }
}
// Modificar el Callback para que imprima la hora LOCAL (con GMT aplicado)
static void time_sync_notification_cb(struct timeval *tv) {
    std::string log_msg = "";
    set_ntp_status(NTP_STATUS_SYNCHRONIZED);
    
    // Imprimir hora legible aplicando la Zona Horaria configurada
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    log_msg = "Hora sincronizada y ajustada (Local): " + std::string(strftime_buf);
    write_system_log(TAG, log_msg.c_str());
    ESP_LOGI(TAG, "%s", log_msg.c_str());
}


ntp_sync_status_t get_ntp_status(void) {
    ntp_sync_status_t status = NTP_STATUS_UNINITIALIZED;
    if (g_ntp_status_mutex && xSemaphoreTake(g_ntp_status_mutex, portMAX_DELAY) == pdTRUE) {
        status = g_ntp_status;
        xSemaphoreGive(g_ntp_status_mutex);
    }
    return status;
}

// --- Tarea Principal Corregida ---
static void ntp_sync_task(void *pvParameter) {
    std::string log_msg = "";
    log_msg = "Iniciando ntp_sync_task.";
    write_system_log(TAG, log_msg.c_str());
    ESP_LOGI(TAG, "%s", log_msg.c_str());
    //ESP_LOGI(TAG, "Iniciando ntp_sync_task...");

    while (1) {
        // 1. ESPERAR CONEXIÓN WI-FI (Usando Event Group)
        // Solo intentamos sincronizar si estamos en modo STA y tenemos el bit de IP
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        if (!(bits & WIFI_CONNECTED_BIT)) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // 2. CONFIGURACIÓN SNTP (Solo si no está inicializado)
        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }

        ESP_LOGI(TAG, "Inicializando SNTP con server: %s", g_ntp_server1);
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, g_ntp_server1);
        esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        
        esp_sntp_init();
        set_ntp_status(NTP_STATUS_SYNCING);

        // 3. ESPERA INICIAL DE SINCRONIZACIÓN (Máximo 60 segundos)
        int retry = 0;
        while (get_ntp_status() != NTP_STATUS_SYNCHRONIZED && retry < 60) {
            // Verificar si el WiFi se cayó durante la espera
            bits = xEventGroupGetBits(s_wifi_event_group);
            if (!(bits & WIFI_CONNECTED_BIT)) {
                ESP_LOGW(TAG, "WiFi perdido durante sincronización NTP.");
                esp_sntp_stop();
                set_ntp_status(NTP_STATUS_FAILED);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry++;
        }
        log_msg = "Resultado NTP: " + std::string((get_ntp_status() == NTP_STATUS_SYNCHRONIZED) ? "SINCRO OK" : "FALLÓ") + 
                  " | TZ: " + (getenv("TZ") ? getenv("TZ") : "UTC");
        write_system_log(TAG, log_msg.c_str()); 
        ESP_LOGI(TAG, "%s", log_msg.c_str());
       // ESP_LOGI(TAG, "Resultado NTP: %s | TZ: %s", 
         //        (get_ntp_status() == NTP_STATUS_SYNCHRONIZED) ? "SINCRO OK" : "FALLÓ",
           //      getenv("TZ") ? getenv("TZ") : "UTC");

        // 4. BUCLE DE MANTENIMIENTO
        // Una vez que intentó sincronizar, esperamos el intervalo largo
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(g_sync_interval_s * 1000));

            bits = xEventGroupGetBits(s_wifi_event_group);
            
            // Si perdemos WiFi, paramos SNTP y volvemos al inicio de la tarea
            if (!(bits & WIFI_CONNECTED_BIT)) {
                log_msg = "WiFi perdido. Reiniciando ciclo de monitoreo NTP.";
                write_system_log(TAG, log_msg.c_str());
                ESP_LOGW(TAG, "%s", log_msg.c_str());
               // ESP_LOGW(TAG, "WiFi perdido. Reiniciando ciclo de monitoreo NTP.");
                esp_sntp_stop();
                set_ntp_status(NTP_STATUS_FAILED);
                break; // Sale al bucle principal para esperar WiFi de nuevo
            }

            // Si por alguna razón el servicio se detuvo pero hay WiFi, re-inicializar
            if (!esp_sntp_enabled()) {
                esp_sntp_init();
                set_ntp_status(NTP_STATUS_SYNCING);
            }
        }
    }
}

// ... (ntp_client_init se mantiene igual) ...
void ntp_client_init(uint32_t sync_interval_s, const char *server1, const char *tz_string) {
    std::string log_msg = "";
    if (g_ntp_status_mutex == NULL) {
        g_ntp_status_mutex = xSemaphoreCreateMutex();
    }
    if (sync_interval_s >= 15) g_sync_interval_s = sync_interval_s;
    if (server1 != NULL) g_ntp_server1 = server1;
    
    log_msg = "Server NTP: " + std::string(g_ntp_server1);
    write_system_log(TAG, log_msg.c_str());
    ESP_LOGI(TAG, "%s", log_msg.c_str());
    //ESP_LOGI(TAG, "Server NTP: %s", g_ntp_server1);

    if (tz_string != NULL) {
        // "TZ" es la variable de entorno estándar de POSIX
        // El 1 indica que debe sobrescribir si ya existe
        setenv("TZ", tz_string, 1);
        tzset(); // Aplica el cambio inmediatamente
        log_msg = "Zona horaria configurada a: " + std::string(tz_string);
        write_system_log(TAG, log_msg.c_str());
        ESP_LOGI(TAG, "%s", log_msg.c_str());
    } else {
        // Por defecto UTC
        setenv("TZ", "UTC0", 1);
        tzset();
    }

    xTaskCreate(&ntp_sync_task, "ntp_sync", 4096, NULL, 5, NULL);
}