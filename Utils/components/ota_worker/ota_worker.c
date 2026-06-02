#include "ota_worker.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_crt_bundle.h"

static const char *TAG = "OTA_WORKER";
static QueueHandle_t s_ota_q = NULL;

static esp_err_t http_event_handler(esp_http_client_event_handle_t evt) { return ESP_OK; }

static bool verify_sha256(const char *expected_hex, const uint8_t *actual, size_t len) {
    if (!expected_hex || !*expected_hex || len != 32) return true;
    for (int i = 0; i < 32; ++i) {
        char hi = expected_hex[i*2], lo = expected_hex[i*2+1]; int v = 0;
        if      (hi>='0'&&hi<='9') v=(hi-'0')<<4; else if (hi>='a'&&hi<='f') v=(hi-'a'+10)<<4; else if (hi>='A'&&hi<='F') v=(hi-'A'+10)<<4; else return false;
        if      (lo>='0'&&lo<='9') v|=(lo-'0');   else if (lo>='a'&&lo<='f') v|=(lo-'a'+10);   else if (lo>='A'&&lo<='F') v|=(lo-'A'+10);   else return false;
        if (actual[i] != (uint8_t)v) return false;
    }
    return true;
}

static void ota_worker_task(void *arg) {
    ota_job_t job;
    while (1) {
        if (xQueueReceive(s_ota_q, &job, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGI(TAG, "OTA: URL='%s' reboot=%d partial=%d chunk=%d", job.url, job.reboot_after, job.partial_download, job.chunk_size);

        esp_http_client_config_t http = { .url = job.url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 15000, .keep_alive_enable = true, .event_handler = http_event_handler };
        esp_https_ota_config_t ota_cfg = { .http_config = &http };
        if (job.partial_download) { ota_cfg.partial_http_download = true; ota_cfg.max_http_request_size = (job.chunk_size>0)?job.chunk_size:4096; }

        esp_https_ota_handle_t handle = NULL; esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
        if (err != ESP_OK) { ESP_LOGE(TAG, "ota_begin falló: %s", esp_err_to_name(err)); continue; }

        esp_app_desc_t app_desc; err = esp_https_ota_get_img_desc(handle, &app_desc);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Nueva imagen: proj='%s' ver='%s' idf='%s'", app_desc.project_name, app_desc.version, app_desc.idf_ver);
            if (!verify_sha256(job.sha256_expected, app_desc.app_elf_sha256, sizeof(app_desc.app_elf_sha256))) {
                ESP_LOGE(TAG, "SHA256 esperada no coincide"); esp_https_ota_abort(handle); continue;
            }
        }

        while (1) {
            err = esp_https_ota_perform(handle);
            if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
            size_t read = esp_https_ota_get_image_len_read(handle);
            size_t total = esp_https_ota_get_image_size(handle);
            ESP_LOGI(TAG, "Descargado: %u / %u", (unsigned)read, (unsigned)total);
        }

        if (esp_https_ota_is_complete_data_received(handle)) err = esp_https_ota_finish(handle);
        else { ESP_LOGE(TAG, "Imagen incompleta"); err = esp_https_ota_abort(handle); }

        if (err == ESP_OK) { ESP_LOGI(TAG, "OTA OK, next boot usará nueva imagen"); if (job.reboot_after) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); } }
        else { ESP_LOGE(TAG, "OTA falló: %s", esp_err_to_name(err)); }
    }
}

bool ota_worker_start(void) {
    s_ota_q = xQueueCreate(4, sizeof(ota_job_t));
    return (s_ota_q && xTaskCreate(ota_worker_task, "ota_worker", 6144, NULL, 6, NULL) == pdPASS);
}

bool ota_submit(const ota_job_t *job) {
    if (!s_ota_q || !job) return false; return xQueueSend(s_ota_q, job, pdMS_TO_TICKS(200)) == pdTRUE;
}
