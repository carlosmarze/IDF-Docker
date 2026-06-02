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
std::string get_current_time() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buf[20];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    return std::string(buf);
}