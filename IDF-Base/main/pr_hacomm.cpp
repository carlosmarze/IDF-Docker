#include "pr_hacomm.h"
#include "config_proyecto.h"
#include "utils_logger.h"
#include "utils_mqtt.h"
#include "utils_config.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include <vector>
#include "esp_log.h"
#include <unordered_map>
#include <cmath>

#define TAG "HA_COMM"

// =========================
// VARIABLES GLOBALES
// =========================

static std::string ha_availability_topic;
static std::string ha_payload_available;
static std::string ha_payload_not_available;

static std::string ha_state_topic[MAX_SENSORS];
static std::string ha_discovery_topic[MAX_SENSORS];
static std::string ha_uniqueid[MAX_SENSORS];

static temp_slot_t temp_slots[MAX_SENSORS];
static bool ha_config_loaded = false; // Indica si la configuración de HA se ha cargado correctamente

extern int SensorID;

//leer el file de configuracion de HA y setear las variables globales para usar en la comunicacion con HA
bool is_ha_config_loaded() {
    return ha_config_loaded;
}

// =========================
// UTILIDAD: leer archivo JSON
// =========================

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

    char *buf = (char*) malloc(len + 1);
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

// =========================
// 1) LEER JSON UNA SOLA VEZ
// =========================

static bool ha_discover_topics(const char *template_json)
{
    cJSON *root = cJSON_Parse(template_json);
    if (!root) {
        LOGE(TAG, "JSON inválido en ha_discover_topics");
        return false;
    }

    // availability_topic
    cJSON *avail_item = cJSON_GetObjectItem(root, "availability_topic");
    if (avail_item && cJSON_IsString(avail_item))
        ha_availability_topic = avail_item->valuestring;
    else
        ha_availability_topic.clear();

    // payload_available
    cJSON *pa_item = cJSON_GetObjectItem(root, "payload_available");
    ha_payload_available = (pa_item && cJSON_IsString(pa_item))
                            ? pa_item->valuestring
                            : "online";

    // payload_not_available
    cJSON *pn_item = cJSON_GetObjectItem(root, "payload_not_available");
    ha_payload_not_available = (pn_item && cJSON_IsString(pn_item))
                                ? pn_item->valuestring
                                : "offline";

    // generar tópicos por sensor
    for (int i = 0; i < MAX_SENSORS; i++) {

        char sensor_name[16];
        snprintf(sensor_name, sizeof(sensor_name), "Temp%d", i+1);

        char st[128];
        snprintf(st, sizeof(st), "%s%d/%s%d",
                 TOPIC_HA_BASE, SensorID, "Temp", i+1);
        ha_state_topic[i] = st;

        char dt[128];
        snprintf(dt, sizeof(dt),
                 "%s%s_%s/config",
                 HA_DISCOVERY_PREFIX, HA_DEVICE_ID, sensor_name);
        ha_discovery_topic[i] = dt;

        char uid[128];
        snprintf(uid, sizeof(uid),
                 "%s_%d_temp%d", HA_DEVICE_ID, SensorID, i+1);
        ha_uniqueid[i] = uid;
    }

    cJSON_Delete(root);
    return true;
}

// =========================
// 2) PUBLICAR DISCOVERY UNA VEZ
// =========================

static bool ha_init_discovery(void)
{
    char *template_json = load_ha_config_template(FILE_HA_CONFIG);
    if (!template_json) {
        LOGE(TAG, "No se pudo cargar " FILE_HA_CONFIG);
        return false;
    }

    if (!ha_discover_topics(template_json)) {
        free(template_json);
        return false;
    }

    // availability inicial
    if (!ha_availability_topic.empty()) {
        publish_mqtt(ha_availability_topic.c_str(),
                     ha_payload_available.c_str(),
                     0, 1);
    }

    // publicar discovery de TempX
    for (int i = 0; i < MAX_SENSORS; i++) {

        cJSON *root = cJSON_Parse(template_json);

        char sensor_name[16];
        snprintf(sensor_name, sizeof(sensor_name), "Temp%d", i+1);

        cJSON_ReplaceItemInObject(root, "name", cJSON_CreateString(sensor_name));
        cJSON_ReplaceItemInObject(root, "state_topic", cJSON_CreateString(ha_state_topic[i].c_str()));
        cJSON_ReplaceItemInObject(root, "unique_id", cJSON_CreateString(ha_uniqueid[i].c_str()));

        char *out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        publish_mqtt(ha_discovery_topic[i].c_str(), out, 0, 1);
        free(out);
    }

    free(template_json);
    ha_config_loaded = true;
    return true;
}

// =========================
// 3) PUBLICAR TEMPERATURAS
// =========================

static void ha_publish_temps(void)
{
    if (!ha_config_loaded) {
        ESP_LOGW(TAG, "HA config no cargada. No se publican temperaturas.");
        return;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        bool publicar = false;
        char payload[32];

        if (temp_slots[i].valid) {
            snprintf(payload, sizeof(payload), "%.2f", temp_slots[i].temp);
            publicar = true;
        }
        else {
            snprintf(payload, sizeof(payload), "nan");
        }

       if(publicar) {
            ESP_LOGI(TAG, "Pub %s -> %s", ha_state_topic[i].c_str(), payload);
            publish_mqtt(ha_state_topic[i].c_str(), payload, 0, 0); // QoS 0, retain 0
        }
        else {
            ESP_LOGW(TAG, "No publicar %s -> %s (sensor inválido)", ha_state_topic[i].c_str(), payload);
        }
    }
}

// =========================
// 4) MAPEAR SENSORES POR ID
// =========================

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
                temp_slots[idx].valid = false;
                ESP_LOGI(TAG, "Temp%d → ID=%s", idx+1, temp_slots[idx].id);
            }
        }
    }

    fclose(f);
    return true;
}

// =========================
// 5) MAPEAR JSON DE TEMPERATURAS
// =========================

void map_sensors_by_id(const char *json)
{
    if (!ha_config_loaded)
        return;

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

        for (int t = 0; t < MAX_SENSORS; t++) {
            if (strcmp(id->valuestring, temp_slots[t].id) == 0) {
                temp_slots[t].temp = temp->valuedouble;
                temp_slots[t].valid = true;
                break;
            }
        }
    }

    cJSON_Delete(root);

    ha_publish_temps();
}

// =========================
// 6) INICIALIZACIÓN GENERAL
// =========================

void ha_init()
{
    if (ha_init_discovery() && load_sensor_map(SENSORSCFG)) {
        LOGI(TAG, "HA config cargada y discovery publicado.");
    } else {
        LOGE(TAG, "Error cargando HA config o publicando discovery.");
    }
}
