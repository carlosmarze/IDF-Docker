#include "utils_time.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <string>


void get_fecha_hora(char *buffer, size_t max_len) {
    time_t now;
    struct tm timeinfo;
    
    // 1. Obtener tiempo actual del sistema
    time(&now);
    
    // 2. Convertir a tiempo local (aplica la TZ configurada en ntp_client)
    localtime_r(&now, &timeinfo);

    // 3. Validar si el tiempo no se ha sincronizado nunca (año 1970)
    if (timeinfo.tm_year < (2016 - 1900)) {
        snprintf(buffer, max_len, "No Sincronizado");
    } else {
        // 4. Formatear: DD/MM/YYYY HH:MM:SS
        strftime(buffer, max_len, "%d/%m/%Y %H:%M:%S", &timeinfo);
    }
}
#include "utils_time.h"
#include <time.h>

// Buffer estático para devolver siempre un const char*
//uso: snprintf(mqttmsg, sizeof(mqttmsg),"Sensor %d Firmware %s online (%s)", SensorID, version_info, get_datetime());
static char datetime_buf[32];

const char* get_datetime()
{
    time_t now;
    time(&now);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Formato: 2026-06-18 17:42:10
    strftime(datetime_buf, sizeof(datetime_buf),
             "%Y-%m-%d %H:%M:%S", &timeinfo);

    return datetime_buf;
}

std::string get_current_time() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buf[20];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    return std::string(buf);
}