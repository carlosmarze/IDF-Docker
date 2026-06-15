#ifndef UTILS_FILES_H
#define UTILS_FILES_H

    #ifdef __cplusplus
        extern "C"  {
    #endif

   #include "esp_littlefs.h"  // Incluye la biblioteca
    // 
    #define MOUNT_POINT "/fs"
    void littlefs_init();
    void print_littlefs_usage();

    #ifdef __cplusplus
        }
    #endif

#endif // MQTT_CLIENT_H