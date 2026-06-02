#ifndef UTILS_OTA_WORKER_H
#define UTILS_OTA_WORKER_H  
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char  url[160];            // URL del binario OTA
    char  sha256_expected[65]; // SHA256 esperado en formato hex (64 chars + '\0')
    bool  reboot_after;        // Reiniciar automáticamente tras OTA exitosa
    bool  partial_download;    // Usar descarga parcial (chunked)
    int   chunk_size;          // Tamaño de cada chunk si partial_download = true
    bool https;
} ota_job_t;
//- Tamaño de cola: lo definís en el .c (xQueueCreate(4, sizeof(ota_job_t))). Si pensás que vas a tener más de 4 jobs en espera, podés parametrizarlo con un 
#define OTA_QUEUE_LENGTH 4 

bool ota_worker_start(void);
bool ota_submit(const ota_job_t *job);
// Variable global (asegúrate de que sea accesible desde app_main y watchdogs)
//bool g_ota_en_progreso = false; Definida en config.h

#ifdef __cplusplus
}
#endif
#endif // UTILS_OTA_WORKER_H
