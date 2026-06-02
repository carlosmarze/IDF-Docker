#ifndef UTILS_NTP_H
#define UTILS_NTP_H

    #ifdef __cplusplus
        extern "C"  {
    #endif
#include <stdint.h>
/**
 * @brief Estados de la sincronización NTP.
 * Usado para informar el status a otras tareas.
 * 
 * Tabla rápida de Strings TZ útiles
 * Lugar    GMT Real    String para el Código (tz_string)
 * Argentina / Uruguay GMT-3 "ART3" o "GMT+3"
 * Chile (Con cambio de hora) GMT-4/3 "CLT4CLST,M9.1.6/24,M4.1.6/24"
 * México (Central) GMT-6 "CST6"
 * Colombia / Perú GMT-5 "EST5"
 * España (Península)GMT+1/+2 "CET-1CEST,M3.5.0,M10.5.0/3"
 * Venezuela GMT-4	"VET4"
 */
typedef enum {
    NTP_STATUS_UNINITIALIZED = 0,
    NTP_STATUS_SYNCING,
    NTP_STATUS_SYNCHRONIZED,
    NTP_STATUS_FAILED // Sincronización fallida (inicial o periódica)
} ntp_sync_status_t;

/**
 * @brief Inicializa el cliente NTP y lanza la tarea de sincronización periódica.
 * @param sync_interval_s Frecuencia de sincronización en segundos (Mínimo recomendado 60s).
 */
void ntp_client_init(uint32_t sync_interval_s,  const char *server1, const char *tz_string);

/**
 * @brief Obtiene el estado actual de la sincronización NTP.
 * @return El estado de sincronización actual.
 */
ntp_sync_status_t get_ntp_status(void);

#ifdef __cplusplus
        }
    #endif
#endif // NTP_CLIENT_H
