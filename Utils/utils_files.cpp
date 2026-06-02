
// =========================================================================
//                             II. LITTLEFS
// =========================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "utils_files.h" // Incluimos nuestro propio header
#include "esp_event.h"
#include "nvs_flash.h"
#include "utils_webs.h"

static const char *TAG = "FS_TASK";

void littlefs_init() {
    ESP_LOGI(TAG, "Inicializando LittleFS...");

    // CORRECCIÓN 2: Inicialización en el orden estricto de la declaración interna
    const esp_vfs_littlefs_conf_t conf = {
      .base_path = MOUNT_POINT,
      .partition_label = "storage", 
      .partition = NULL,             // <-- ORDEN CORREGIDO
      .format_if_mount_failed = true, // <-- ORDEN CORREGIDO
      .read_only = false,            // <-- ORDEN CORREGIDO
      .dont_mount = false,           // <-- ORDEN CORREGIDO
      .grow_on_mount = false         // <-- ORDEN CORREGIDO
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Fallo al montar o formatear LittleFS.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "No se encontró la partición LittleFS (label: 'storage').");
        } else {
            ESP_LOGE(TAG, "Fallo al inicializar LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    ESP_LOGI(TAG, "LittleFS montado correctamente.");
}
