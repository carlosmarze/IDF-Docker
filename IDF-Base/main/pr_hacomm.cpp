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

static std::string ha_availability_topic;
static std::string ha_payload_available;
static std::string ha_payload_not_available;
static std::string ha_state_topic[MAX_SENSORS];
static std::string ha_discovery_topic[MAX_SENSORS];


//leer el file de configuracion de HA y setear las variables globales para usar en la comunicacion con HA
bool is_ha_config_loaded() {
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
        bool publicar = false;
        char state_topic[64];
        
        char payload[32];

        if (temp_slots[i].valid) {
            snprintf(payload, sizeof(payload), "%.2f", temp_slots[i].temp);
            publicar = true;
        } else {
            snprintf(payload, sizeof(payload), "nan");  // HA lo marca unavailable
        }

        if(publicar) {
            ESP_LOGI(TAG, "Pub %s -> %s", state_topic, payload);
            publish_mqtt(ha_state_topic[i].c_str(), payload, 0, 0); // QoS 0, retain 0
        }
        else {
            ESP_LOGW(TAG, "No publicar %s -> %s (sensor inválido)", ha_state_topic[i].c_str(), payload);
        }
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
//lee los tópicos que están en el json e configuración
static bool ha_discover_topics(const char *template_json)
{
    cJSON *root = cJSON_Parse(template_json);
    if (!root) {
        LOGE(TAG, "JSON inválido en ha_discover_topics");
        return false;
    }

    // 1) Leer availability_topic
    cJSON *avail_item = cJSON_GetObjectItem(root, "availability_topic");
    if (avail_item && cJSON_IsString(avail_item)) {
        ha_availability_topic = avail_item->valuestring;
    } else {
        LOGW(TAG, "availability_topic no encontrado");
        ha_availability_topic.clear();
    }

    // 2) Leer payload_available
    cJSON *pa_item = cJSON_GetObjectItem(root, "payload_available");
    ha_payload_available = (pa_item && cJSON_IsString(pa_item))
                            ? pa_item->valuestring
                            : "online";

    // 3) Leer payload_not_available
    cJSON *pn_item = cJSON_GetObjectItem(root, "payload_not_available");
    ha_payload_not_available = (pn_item && cJSON_IsString(pn_item))
                                ? pn_item->valuestring
                                : "offline";

    // 4) Leer state_topic base
    cJSON *state_item = cJSON_GetObjectItem(root, "state_topic");
    std::string state_base;

    if (state_item && cJSON_IsString(state_item)) {
        state_base = state_item->valuestring;
    } else {
        LOGE(TAG, "state_topic no encontrado en JSON");
        cJSON_Delete(root);
        return false;
    }

    // 5) Construir los tópicos para Temp1..Temp4
    for (int i = 0; i < HA_SENSOR_COUNT; i++) {

        char sensor_name[16];
        snprintf(sensor_name, sizeof(sensor_name), "Temp%d", i+1);

        // state_topic final
        char st[128];
        snprintf(st, sizeof(st), "%s%d/%s%d",
                 TOPIC_HA_BASE, SensorID, "Temp", i+1);
        ha_state_topic[i] = st;

        // discovery_topic final
        char dt[128];
        snprintf(dt, sizeof(dt),
                 "%s%s_%s/config",
                 HA_DISCOVERY_PREFIX, HA_DEVICE_ID, sensor_name);
        ha_discovery_topic[i] = dt;
    }

    cJSON_Delete(root);
    return true;
}



static bool ha_init_discovery(void)
{
    char *template_json = load_ha_config_template(FILE_HA_CONFIG);
    if (!template_json) {
        LOGE(TAG, "No se pudo cargar " FILE_HA_CONFIG);
        return false;
    }

    // 1) Descubrir tópicos
    if (!ha_discover_topics(template_json)) {
        free(template_json);
        return false;
    }

    // 2) Publicar availability inicial
    if (!ha_availability_topic.empty()) {
        publish_mqtt(ha_availability_topic.c_str(),
                     ha_payload_available.c_str(),
                     0,
                     1);  // retain=1
    }

    // 3) Publicar discovery de Temp1..Temp4
    for (int i = 0; i < HA_SENSOR_COUNT; i++) {

        // Parsear JSON base
        cJSON *root = cJSON_Parse(template_json);

        // Modificar name
        char sensor_name[16];
        snprintf(sensor_name, sizeof(sensor_name), "Temp%d", i+1);
        cJSON_ReplaceItemInObject(root, "name", cJSON_CreateString(sensor_name));

        // Modificar state_topic
        cJSON_ReplaceItemInObject(root, "state_topic",
                                  cJSON_CreateString(ha_state_topic[i].c_str()));

        // Serializar
        char *out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        publish_mqtt(ha_discovery_topic[i].c_str(), out, 0, 1);
        free(out);
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