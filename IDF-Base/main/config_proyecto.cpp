#include "esp_log.h"
#include <stdio.h>      // Para fopen, fclose, fgets
#include <string.h>     // Para strstr, strcspn, strtok
//using namespace std; //si no, hay que poner std:: en todo y usar string.h
#include <stdlib.h>     // Para atoi
#include <vector>
#include "driver/gpio.h"
#include "esp_littlefs.h" // Asegurarse de que el FS esté montado antes de llamar a esta función
//#include "utils_files.h" // Para MOUNT_POINT
#include "utils_config.h"
#include "utils_cmd_dispatcher.h" //para usar dispatcher
#include "utils_logger.h"

#include "config_proyecto.h"
#include "pr_onewire.h"

#define TAG_CONFIG "PROYCONFIG"



static bool aplicar_config_proyecto_linea_directo(const char* linea) {
    if (strncmp(linea, "setonewirepin=", 14) == 0) {
        g_onewire_pin = atoi(linea + 14);
        LOGI(TAG_CONFIG, "OneWire pin: %d", g_onewire_pin);
        return true;
    }
    
    /*
    if (strncmp(linea, "Temp1=", 5) == 0) {
        temp1 = (strcmp(linea + 5, "true") == 0);
        LOGI(TAG_CONFIG, "Temp1: %s", temp1 ? "true" : "false");
        return true;
    }
    
    if (strncmp(linea, "setesquema=", 11) == 0) {
        strncpy(esquema, linea + 11, sizeof(esquema) - 1);
        esquema[sizeof(esquema) - 1] = '\0';
        LOGI(TAG_CONFIG, "esquema: %s", esquema);
        return true;
    }
    if (strncmp(linea, "setwebuser=", 11) == 0) {
        strncpy(webuser, linea + 11, sizeof(webuser) - 1);
        webuser[sizeof(webuser) - 1] = '\0';
        LOGI(TAG_CONFIG, "Web user: %s", webuser);
        return true;
    }
    if (strncmp(linea, "setwebpass=", 11) == 0) {
        strncpy(webpass, linea + 11, sizeof(webpass) - 1);
        webpass[sizeof(webpass) - 1] = '\0';
        LOGI(TAG_CONFIG, "Web password: %s", webpass);
        return true;
    }
    if (strncmp(linea, "autowebupdate=", 14) == 0) {
        autowebupdate = (strcmp(linea + 14, "true") == 0);
        LOGI(TAG_CONFIG, "Auto WebUpdate al inicio: %s", autowebupdate ? "true" : "false");
        return true;
    }
    if (strncmp(linea, "setmqttuser=", 12) == 0) {
        strncpy(mqttuser, linea + 12, sizeof(mqttuser) - 1);
        mqttuser[sizeof(mqttuser) - 1] = '\0';
        LOGI(TAG_CONFIG, "Mqtt user: %s", mqttuser);
        return true;
    }
    if (strncmp(linea, "setmqttpass=", 12) == 0) {
        strncpy(mqttpass, linea + 12, sizeof(mqttpass) - 1);
        mqttpass[sizeof(mqttpass) - 1] = '\0';
        LOGI(TAG_CONFIG, "Mqtt pass: %s", mqttpass);
        return true;
    }
    if (strncmp(linea, "setmqtthost=", 12) == 0) {
        strncpy(mqtthost, linea + 12, sizeof(mqtthost) - 1);
        mqtthost[sizeof(mqtthost) - 1] = '\0';
        LOGI(TAG_CONFIG, "Mqtt Broker Host: %s", mqtthost);
        return true;
    }
    if (strncmp(linea, "setmqtttbase=", 13) == 0) {
        strncpy(mqtttopicbase, linea + 13, sizeof(mqtttopicbase) - 1);
        mqtttopicbase[sizeof(mqtttopicbase) - 1] = '\0';
        LOGI(TAG_CONFIG, "Mqtt topic base: %s", mqtttopicbase);
        return true;
    }

    if (strncmp(linea, "setmqttcert=", 12) == 0) {
        char temp[128 + 8]; //buffer para armar mqttcert sin desperdiciar espacio permanentemente. Tamaño de linea + /fs/
        const char* value = linea + 12; //Acá linea es el puntero al buffer, posición 0
        // Caso 1: el usuario NO puso barra inicial → agregamos "/"
        if (value[0] != '/') {
            snprintf(temp, sizeof(temp),"%s/%s", MOUNT_POINT, value); //para no usar temp habría que tener un buffer mayor al size de linea + mount point
        }
        // Caso 2: el usuario sí puso barra inicial → concatenamos directo
        else {
            snprintf(temp, sizeof(temp),"%s%s", MOUNT_POINT, value);
        }
        strncpy(mqttcert, temp, sizeof(mqttcert));
        mqttcert[sizeof(mqttcert) - 1] = '\0';
        LOGI(TAG_CONFIG, "Mqtt root certificate: %s", mqttcert);
        return true;
    }

    if (strncmp(linea, "setmqttport=", 12) == 0) {
        mqttport = (uint32_t) strtoul(linea + 12, NULL, 10);
        LOGI(TAG_CONFIG, "Mqtt port: %u", mqttport);
        return true;
    }
    if (strncmp(linea, "setblename=", 11) == 0) {
        strncpy(blename, linea + 11, sizeof(blename) - 1);
        blename[sizeof(blename) - 1] = '\0';
        LOGI(TAG_CONFIG, "BlueTooth name: %s", blename);
        return true;
    }
    */
    // otros comandos de config que solo setean variables
    

    //setmqttserver
    //setmqttport
    //setmqttuser
    //setmqttpass
    //setmqtttopic
    return true;
}

bool cargar_config_proyecto_file_directo() {
    bool retorno = false;
    FILE* f = fopen(CONFIG_FILE_PATH, "r");
    if (!f) return retorno;

    char linea[128];
    while (fgets(linea, sizeof(linea), f)) {
        linea[strcspn(linea, "\r\n")] = 0;
        if (linea[0] == '#' || strlen(linea) < 3) continue;
        retorno = true;
        aplicar_config_proyecto_linea_directo(linea);
    }
    fclose(f);
    return retorno;
}
