#include "pr_hacomm.h"
#include "config_proyecto.h"
#include "utils_logger.h"
#include "utils_mqtt.h"
#include "utils_config.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include <vector>
#include "esp_log.h"
#include <unordered_map> //para reading_changed
#include <cmath> //para reading_changed

#define TAG "HA_COMM"

//leer el file de configuracion de HA y setear las variables globales para usar en la comunicacion con HA
bool ha_config_loaded_func() {
    return ha_config_loaded;
}

static char *load_ha_config_template(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGE(TAG, "Config: No se pudo abrir %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    auto *buf = (char*) malloc(len + 1);
    if (!buf) {
        LOGE(TAG, "Sin memoria para config");
        fclose(f);
        return NULL;
    }

    size_t r = fread(buf, 1, len, f);
    fclose(f);

    buf[r] = '\0';
    return buf;
}


//Publicar configuración de descubrimiento de sensores a Home Assistant
static void ha_publish_temp_config_for_sensor(int index,
                                              const char *template_json)
{
    // index: 1..4
    char sensor_name[16];
    snprintf(sensor_name, sizeof(sensor_name), "Temp%d", index);

    // state_topic: miTSESP/HA/7001/Temp1, Temp2, etc.
    char state_topic[64];
    snprintf(state_topic, sizeof(state_topic), "%s%d/%s%d",
             TOPIC_HA_BASE, SensorID, "Temp", index);

    // discovery topic: homeassistant/sensor/esp32_living_Temp1/config
    char discovery_topic[128];
    snprintf(discovery_topic, sizeof(discovery_topic),
             "%s%s_%s/config",
             HA_DISCOVERY_PREFIX, HA_DEVICE_ID, sensor_name);

    // Parsear plantilla
    cJSON *root = cJSON_Parse(template_json);
    if (!root) {
        LOGE(TAG, "Error parseando JSON plantilla");
        return;
    }

    // Modificar "name"
    cJSON *name_item = cJSON_GetObjectItem(root, "name");
    if (name_item && cJSON_IsString(name_item)) {
        cJSON_SetValuestring(name_item, sensor_name);
    } else {
        cJSON_ReplaceItemInObject(root, "name", cJSON_CreateString(sensor_name));
    }

    // Modificar "state_topic"
    cJSON *state_item = cJSON_GetObjectItem(root, "state_topic");
    if (state_item && cJSON_IsString(state_item)) {
        cJSON_SetValuestring(state_item, state_topic);
    } else {
        cJSON_ReplaceItemInObject(root, "state_topic", cJSON_CreateString(state_topic));
    }

    // Serializar JSON final
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!out) {
        LOGE(TAG, "Error serializando JSON");
        return;
    }

    LOGI(TAG, "Publicando discovery %s -> %s", discovery_topic, out);
    publish_mqtt(discovery_topic, out, 0, 1); // QoS 0, retain 1
    //esp_mqtt_client_publish(mqtt_client, discovery_topic, out, 0, 1, 1);
    /*  Retain = 1: 
        Home Assistant no guarda ese mensaje internamente, sino que lo vuelve a leer cada vez que reinicia o cuando se reconecta al broker.
        Si el mensaje no está retenido (retain=0):
        Si HA se reinicia → pierde la entidad
        Si HA se reconecta al broker → pierde la entidad
        Si tu ESP32 no publica el config en ese momento → no aparece el sensor
    */
    free(out);
    ha_config_loaded = true;

}


//Publicar valores de temperatura a Home Assistant
static void ha_publish_temps(void)
{
    if(!ha_config_loaded) {
        ESP_LOGW(TAG, "HA config no cargada. No se publican temperaturas.");
        return;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        char state_topic[64];
        snprintf(state_topic, sizeof(state_topic), "%sTemp%d", TOPIC_HA_BASE, i+1);

        char payload[32];

        if (temp_slots[i].valid) {
            snprintf(payload, sizeof(payload), "%.2f", temp_slots[i].temp);
        } else {
            snprintf(payload, sizeof(payload), "nan");  // HA lo marca unavailable
        }

        ESP_LOGI(TAG, "Pub %s -> %s", state_topic, payload);
        publish_mqtt(state_topic, payload, 0, 0); // QoS 0, retain 0
        // esp_mqtt_client_publish(mqtt_client, state_topic, payload, 0, 1, 0);
        /* Retain = 0:
            Sensores de temperatura → retain=0
            Porque la temperatura cambia constantemente.
            Si usás retain=1, HA podría mostrar un valor viejo si el ESP32 se desconecta.

            ✔️ Sensores binarios (interruptores) → retain=1
            Porque si el ESP32 se reinicia, HA debe saber el último estado.

            ✔️ Switches / relés → retain=1 en state_topic
            Para que HA recuerde si estaba ON u OFF.

            ✔️ Energía acumulada (kWh) → retain=1
            Porque es un valor que debe persistir.

            ✔️ Potencia instantánea (W) → retain=0
            Porque cambia todo el tiempo.
        */
    }
}


static bool ha_init_discovery(void)
{
    char *template_json = load_ha_config_template(FILE_HA_CONFIG);
    if (!template_json) {
        LOGE(TAG, "No se pudo cargar " FILE_HA_CONFIG);
        return false;
    }

    for (int i = 1; i <= HA_SENSOR_COUNT; ++i) {
        ha_publish_temp_config_for_sensor(i, template_json);
    }

    free(template_json);
    return true;
}


static bool load_sensor_map(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGE(TAG, "Map: No se pudo abrir %s", path);
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[16], value[32];

        if (sscanf(line, "%15[^=]=%31s", key, value) == 2) {
            int idx = -1;

            if (strcmp(key, "Temp1") == 0) idx = 0;
            else if (strcmp(key, "Temp2") == 0) idx = 1;
            else if (strcmp(key, "Temp3") == 0) idx = 2;
            else if (strcmp(key, "Temp4") == 0) idx = 3;

            if (idx >= 0) {
                strncpy(temp_slots[idx].id, value, sizeof(temp_slots[idx].id)-1);
                temp_slots[idx].valid = false;   // se marcará luego
                ESP_LOGI(TAG, "Temp%d → ID=%s", idx+1, temp_slots[idx].id);
            }
        }
    }

    fclose(f);
    return true;
}

void map_sensors_by_id(const char *json) //Esta se invoca cuando ya hay un json generado por el comando tempscan, que contiene los sensores leídos y sus temperaturas. Se actualiza el mapa de sensores por ID y se publica en HA.
{
    // Inicializar como inválidos
    if(!ha_config_loaded) {
        return; //si no se cargó la config de HA, no hacemos nada
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        temp_slots[i].temp = -999.0f;
        temp_slots[i].valid = false;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        LOGE(TAG, "JSON inválido");
        return;
    }

    cJSON *arr = cJSON_GetObjectItem(root, "sensors");
    if (!arr || !cJSON_IsArray(arr)) {
        LOGE(TAG, "JSON: No existe 'sensors'");
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(arr);

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *temp = cJSON_GetObjectItem(item, "temp");

        if (!id || !cJSON_IsString(id) || !temp || !cJSON_IsNumber(temp))
            continue;

        // Buscar qué TempX corresponde a este ID
        for (int t = 0; t < MAX_SENSORS; t++) {
            if (strcmp(id->valuestring, temp_slots[t].id) == 0) {
                temp_slots[t].temp = temp->valuedouble;
                temp_slots[t].valid = true;
                break;
            }
        }
    }

    cJSON_Delete(root);
    // Publicar en Home Assistant
    ha_publish_temps();
}


void ha_init()
{
    // Cargar mapa de sensores desde archivo
    if(ha_init_discovery() && load_sensor_map(SENSORSCFG)) {
        LOGI(TAG, "HA config cargada y discovery publicado.");
        ha_config_loaded = true;
    } else {
        LOGE(TAG, "Error cargando HA config o publicando discovery.");
    }
    // Publicar configuración de descubrimiento en Home Assistant
    
}