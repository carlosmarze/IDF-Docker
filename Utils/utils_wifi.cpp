#include <string>
#include "esp_log.h"
#include "esp_wifi.h"       // Para la API de Wi-Fi
#include "esp_mac.h"      
#include "esp_system.h"     
#include "freertos/event_groups.h" 
#include "lwip/err.h"       
#include "lwip/sys.h"       
#include "esp_event.h"      // Para el event loop
#include "cJSON.h"  //para save_wifi_network
#include <stdio.h>  //para save_wifi_network

#include "utils_wifi.h"
#include "utils_mqtt.h" // Para detener MQTT si se pierde Wi-Fi
#include "utils_logger.h" // Para logging
#include "utils_config.h" // Para save_wifi_network
#include "utils_events.h" // Para dejar eventos en la cola global

// --- CONFIGURACIÓN DE RED ---
#define TAG "WIFI_SETUP"
//#define EXAMPLE_ESP_WIFI_SSID      "Departamento5"
//#define EXAMPLE_ESP_WIFI_PASS      "el departamento es azul"
//#define EXAMPLE_ESP_MAXIMUM_RETRY  5

// Variables de Estado
int s_retry_num = 0;
bool wifi_ready = false;
bool g_manual_wifi_connect = false;
volatile bool g_eapol_fail = false; // Variable  para detectar fallos EAPOL1
//bool g_ota_en_progreso = false;

EventGroupHandle_t s_wifi_event_group = NULL;


//Variables para hablar con otras funciones

// --- NUEVAS DEPENDENCIAS ---
#include "freertos/semphr.h" // Para semáforos

// --- CONSTANTES DE TIEMPO ---
#define WIFI_CHECK_INTERVAL_MS (5 * 60 * 1000UL) // 5 minutos en milisegundos

// --- 1. PROTOTIPOS DE FUNCIONES INTERNAS ---
// Necesario porque event_handler llama a set_wifi_state, y set_wifi_state está definida abajo.
//static void set_wifi_state(wifi_state_t new_state); 
//static void wifi_check_task(void *pvParameter);


// Variable global para el estado actual (protegida por Mutex)
//static wifi_state_t g_current_wifi_state = MY_WIFI_MODE_NONE;
// Mutex para proteger la variable global de estado
static SemaphoreHandle_t g_wifi_state_mutex;

static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    log_event_t levt;
    bool log_event = false;
    
    switch (event_id) {

        case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t* ev = (wifi_event_sta_disconnected_t*)event_data;

                // Detectar EAPOL1 / deauth
                if (ev->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    ev->reason == WIFI_REASON_AUTH_EXPIRE ||
                    ev->reason == WIFI_REASON_MIC_FAILURE ||
                    ev->reason == WIFI_REASON_HANDSHAKE_TIMEOUT) 
                {
                    g_eapol_fail = true;
                } else {
                    g_eapol_fail = false;
                }
                //fin detección EAPOL1

                if (g_manual_wifi_connect) {
                    levt = LOG_EVT_WIFI_DISCONNECTED_MANUAL;
                    log_event = true;
                } else {
                    levt = LOG_EVT_WIFI_DISCONNECTED;
                    log_event = true;
                }

                // Estado interno
                wifi_ready = false;

                // Limpiar bit de conexión
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                // Despertar a conectar_wifi() / esperar_conexion()
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);

                // Cortar MQTT si corresponde
                if (!g_ota_en_progreso) {
                    mqtt_app_stop();
                }

                // 👇 IMPORTANTE:
                // NO reconectar acá
                // NO s_retry_num++
                // NO esp_wifi_connect()
                // La reconexión la maneja conectar_wifi() y la estrategia maestra
                break;
            }

        case WIFI_EVENT_AP_START:
            levt = LOG_EVT_WIFI_AP_START;
            log_event = true;
            wifi_ready = false;
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            levt = LOG_EVT_WIFI_AP_STACONNECTED;
            log_event = true;
            wifi_ready = false;
            break;
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI STA_START");
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "WIFI STA_STOP");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi STA conectado");
            wifi_ready = true;
            break;
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
            ESP_LOGI(TAG, "WIFI_EVENT_HOME_CHANNEL_CHANGE");
            //wifi_ready = true;
            break;
            
         case WIFI_EVENT_STA_BEACON_TIMEOUT:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_BEACON_TIMEOUT");
            //wifi_ready = true;
            break;
        default:
            ESP_LOGW(TAG, "WIFI_EVENT desconocido id=%ld", event_id);
            break;
    }

    if (log_event) {
        xQueueSend(g_log_queue, &levt, portMAX_DELAY);
    }
}


//Handler eventos IP (obtener IP, perder IP)
static void ip_event_handler(void* arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void* event_data)
{
    log_event_t levt; //nro de evento a loggear en la cola global del logger
    bool log_event = false; // Si el evento es relevante para loggear (ej: desconexiones, conexiones, etc.)

    switch (event_id) {

    case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        char ipbuf[16];
        snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&event->ip_info.ip));
        events_set_last_ip(ipbuf);

        s_retry_num = 0;
        wifi_ready = true;

        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        levt = LOG_EVT_WIFI_GOT_IP;
        log_event = true;
        ESP_LOGI(TAG, "IP Obtenida: %s", ipbuf);

        // Si querés iniciar MQTT automáticamente:
        if (!g_ota_en_progreso) {
            //mqtt_app_start();
        }

        break;
    }

    case IP_EVENT_STA_LOST_IP:
        levt = LOG_EVT_WIFI_LOST_IP;
        log_event = true;
        ESP_LOGW(TAG, "IP Perdida.");
        wifi_ready = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (!g_ota_en_progreso) {
            mqtt_app_stop();
        }
        break;

    default:
        ESP_LOGW(TAG, "IP_EVENT desconocido id=%ld", event_id);
        break;
    }//fin del switch

    if (log_event) {
        //write_system_log(TAG, log_msg.c_str());
        xQueueSend(g_log_queue, &levt, portMAX_DELAY);
        //ESP_LOGI(TAG, "%s", log_msg.c_str()); 
    }
}

//Para conectar con otras funciones
/**
 * @brief Obtiene el estado actual de la conexión Wi-Fi (Cliente o AP).
 * @return El valor enum de wifi_state_t.
 */

 /*
 wifi_state_t get_wifi_state(void) {
    wifi_state_t state = MY_WIFI_MODE_NONE;
    if (g_wifi_state_mutex != NULL && xSemaphoreTake(g_wifi_state_mutex, portMAX_DELAY) == pdTRUE) {
        state = g_current_wifi_state;
        xSemaphoreGive(g_wifi_state_mutex);
    }
    return state;
}
*/

/**
 * @brief Establece el estado actual de la conexión Wi-Fi.
 */
// --- DEFINICIÓN DE FUNCIÓN INTERNA (NO EN EL .h) ---
// Notar que la palabra 'static' la hace privada a este archivo.
/*
static void set_wifi_state(wifi_state_t new_state) {
    if (g_wifi_state_mutex != NULL && xSemaphoreTake(g_wifi_state_mutex, portMAX_DELAY) == pdTRUE) {
        g_current_wifi_state = new_state;
        xSemaphoreGive(g_wifi_state_mutex);
        //Desconecto también el cliente MQTT si la conexión Wi-Fi se pierde
        if (new_state == MY_WIFI_MODE_DISCONNECTED) {
            // Detener MQTT si está activo
            //extern void mqtt_app_stop(void); // Declaración externa, debe ir en el .h
            mqtt_app_stop();
        }
    }
}
*/

/***************************************************************
 * Esta tarea se encargará de intentar una conexión asincronicamente, si es despertada por una notificación de otra tarea Crearla desde app_main().
 **************************************************************************/
TaskHandle_t wifi_connect_task_handle = nullptr;

void wifi_connect_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        iniciar_proceso_conexion_maestra();
    }
}


/***************************************************************
 * Esta tarea se encargará de despertar cada 5 minutos y reintentar la conexión si el estado es DISCONNECTED.
 **************************************************************************/

// Añadimos una bandera simple
bool g_sistema_inicializando = true;

void wifi_check_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(300000)); // 5 minutos

        if (g_ota_en_progreso || g_sistema_inicializando) {
            LOGI(TAG, "Watchdog WiFi: Sistema ocupado (OTA o Init), saltando chequeo.");
            //write_system_log(TAG, log_msg.c_str());
            //ESP_LOGI(TAG, "%s", log_msg.c_str());
            continue;
        }

        wifi_mode_t mode;
        esp_wifi_get_mode(&mode); // Función nativa del sistema

        // Si el sistema está en modo AP, no molestamos con reconexiones
        if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
            LOGI(TAG, "Equipo en modo AP. Chequeando WiFi...");
            //ESP_LOGI(TAG, "Equipo en modo AP. Chequeando WiFi...");
            //continue;
        }

        // Si estamos en modo STA, verificamos si tenemos los bits de conexión
        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            LOGW(TAG, "WiFi desconectado detectado por watchdog. Reintentando...");
            iniciar_proceso_conexion_maestra();
        }
    }
}

//Wifi hardware init sin conectar
extern "C" void wifi_hardware_init(void) {
    LOGI(TAG, "Iniciando wifi HW.");
    
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
    }
    if (g_wifi_state_mutex == NULL) {
        g_wifi_state_mutex = xSemaphoreCreateMutex();
    }

    // Nota: esp_netif_init() ya se llamó en app_main antes de esta función
    esp_netif_create_default_wifi_sta(); 

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    LOGI(TAG, "fin wifi HW.");
    // Tarea quitada de aquí para lanzarla en el momento justo
}
//Wifi Conectar con SSID y PASS dinámicos
bool conectar_wifi(const char* ssid, const char* pass, int timeout_ms)
{
    const int MAX_RETRIES = 5;
    const int RESET_DRIVER_EVERY = 3;

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Intentando conectar a SSID: %s", ssid);
    LOGI(TAG, "%s", buffer);

    for (int intento = 1; intento <= MAX_RETRIES; intento++) {

        LOGI(TAG, "[%s] Intento %d/%d", ssid, intento, MAX_RETRIES);

        // Reset del driver cada 3 intentos
        if (intento % RESET_DRIVER_EVERY == 0) {
            LOGW(TAG, "Reset del driver WiFi antes del intento %d", intento);

            esp_wifi_stop();
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_wifi_start();
            vTaskDelay(pdMS_TO_TICKS(300));
        }

        // Limpiar flags
        g_eapol_fail = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

        // Configurar SSID/PASS
        wifi_config_t wifi_config = {};
        strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

        // Conectar
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_wifi_connect();

        // Esperar resultado
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(timeout_ms)
        );

        if (bits & WIFI_CONNECTED_BIT) {
            LOGI(TAG, "[%s] Conectado exitosamente", ssid);
            return true;
        }

        // Si falló, analizar motivo
        if (g_eapol_fail) {
            ESP_LOGW(TAG, "[%s] Fallo EAPOL/Handshake. Reintentando...", ssid);
        } else {
            ESP_LOGW(TAG, "[%s] Fallo genérico. Reintentando...", ssid);
        }

        // Backoff progresivo
        int backoff_ms = 300 + (intento * 300);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
    }

    LOGE(TAG, "[%s] Fallaron todos los intentos", ssid);
    return false;
}


bool conectar_wifiOLD(const char* ssid, const char* pass, int timeout_ms) {
    // 1. Detenemos cualquier intento previo
    LOGI(TAG, "disconnect wifi: " );
    //write_system_log(TAG, log_msg.c_str());
    esp_wifi_disconnect();
    s_retry_num = 0;  // <--- ¡ESTO ES LO QUE FALTABA!

    // 2. Preparar configuración
    wifi_config_t wifi_config = {};
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    // 3. Aplicar configuración (SOLO UNA VEZ)
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        //log_msg = "Error al configurar WiFi: " + std::to_string(err);
        //write_system_log(TAG, log_msg.c_str());
        LOGE(TAG, "Error WiFi (0x%x)", err);
        //ESP_LOGE(TAG, "Error WiFi (0x%x)", err);
        return false;
    }

    // 4. Preparar Log
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "Conectando a SSID: %s", ssid);
    //write_system_log(TAG, buffer);
    LOGI(TAG, "%s", buffer);
    //ESP_LOGI(TAG, "%s", buffer);
    
    // 5. Limpiar bits y Conectar
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Eliminamos la línea repetida de set_config que causaba el crash
    err = esp_wifi_connect(); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al llamar a connect (0x%x)", err);
        return false;
    }

    // 6. Esperar resultado
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        snprintf(buffer, sizeof(buffer), "Exito! Guardando %s en el historial.", ssid);
        //write_system_log(TAG, buffer);
        //ESP_LOGI(TAG, "%s", buffer);
        LOGI(TAG, "%s", buffer);
        return true;
    } 
    
    esp_wifi_disconnect();
    return false;
}

//Inicar modo AP si todo falla 
void iniciar_modo_ap(const char* ssid, const char* pass) {
    LOGI(TAG, "Configurando Modo AP de rescate...");

    wifi_config_t ap_config = {};
    
    // Usamos los parámetros que entran a la función (ssid y pass)
    strlcpy((char*)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    strlcpy((char*)ap_config.ap.password, pass, sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 1;

    // Seguridad: si no hay password, el AP queda abierto
    if (strlen(pass) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    // 1. Cambiamos el modo a AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // 2. Aplicamos la configuración
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    
    // Log de confirmación
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "AP Listo. SSID: %s, Pass: %s", ssid, (strlen(pass) > 0 ? pass : "OPEN"));
    //write_system_log(TAG, buffer);
    //ESP_LOGI(TAG, "%s", buffer);
    LOGI(TAG, "%s", buffer);
}

//Funciones para obtener info de la conexión actual
std::string get_connected_ssid() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return std::string((char*)ap_info.ssid);
    }
    return "N/A";
}
std::string get_local_ip() {
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char buf[16];
        esp_ip4addr_ntoa(&ip_info.ip, buf, sizeof(buf));
        return std::string(buf);
    }
    return "0.0.0.0";
}
std::string get_mac_address() {
    uint8_t mac[6];
    char mac_str[18];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_str);
}
//guardado de las últimas redes wifi conectadas


//#define WIFI_JSON_FILE "/wifi.json" en utils_config.h

void save_wifi_network(const char* ssid, const char* pass) {
    FILE* f = fopen(WIFI_JSON_FILE, "r");
    cJSON* root = NULL;

    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* data = (char*)malloc(size + 1);
        fread(data, 1, size, f);
        data[size] = '\0';
        fclose(f);

        root = cJSON_Parse(data);
        free(data);
    }

    if (!root) {
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "last_used", 0);
        cJSON_AddItemToObject(root, "networks", cJSON_CreateArray());
    }

    cJSON* networks = cJSON_GetObjectItem(root, "networks");
    int total = cJSON_GetArraySize(networks);
    int found_index = -1;

    // Buscar si ya existe
    for (int i = 0; i < total; i++) {
        cJSON* item = cJSON_GetArrayItem(networks, i);
        const char* ssid_json = cJSON_GetObjectItem(item, "ssid")->valuestring;

        if (strcmp(ssid_json, ssid) == 0) {
            found_index = i;

            // 🔥 NUEVO: actualizar password si cambió
            cJSON* pass_item = cJSON_GetObjectItem(item, "pass");
            if (!pass_item || strcmp(pass_item->valuestring, pass) != 0) {
                cJSON_ReplaceItemInObject(item, "pass", cJSON_CreateString(pass));
            }

            break;
        }
    }

    if (found_index != -1) {
        // Ya existía → actualizar last_used
        cJSON_ReplaceItemInObject(root, "last_used", cJSON_CreateNumber(found_index));
    } else {
        // Nuevo → agregarlo
        cJSON* new_item = cJSON_CreateObject();
        cJSON_AddStringToObject(new_item, "ssid", ssid);
        cJSON_AddStringToObject(new_item, "pass", pass);
        cJSON_AddItemToArray(networks, new_item);

        cJSON_ReplaceItemInObject(root, "last_used", cJSON_CreateNumber(total));
    }

    // Guardar archivo
    char* nuevo_json = cJSON_Print(root);
    FILE* f_write = fopen(WIFI_JSON_FILE, "w");

    if (f_write) {
        fputs(nuevo_json, f_write);
        fclose(f_write);
    }

    //write_system_log(TAG, nuevo_json);
    LOGI(TAG, "%s", nuevo_json);

    free(nuevo_json);
    cJSON_Delete(root);
}

/*
static void reset_wifi_retry_count() {
    s_retry_num = 0;
    // También limpiamos los bits para empezar de cero el nuevo intento
    if (s_wifi_event_group != NULL) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
}
*/
// Función para intentar conectar usando el historial
bool conectar_wifi_desde_json() {
    //std::string log_msg = "Usando historial de " + std::string(WIFI_JSON_FILE);
    LOGI(TAG, "Usando historial de %s", WIFI_JSON_FILE);
    
    FILE* f = fopen(WIFI_JSON_FILE, "r");
    //write_system_log(TAG, log_msg.c_str());
    if (!f) {
        LOGI(TAG, "No se encontro json Wi-Fi.");
        //ESP_LOGW(TAG, "No se encontró historial Wi-Fi.");
        return false;
    }
    
    // ... (Lectura de archivo igual que antes) ...
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = (char*)malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = '\0';
    fclose(f);
    cJSON* root = cJSON_Parse(data);
    free(data);
    if (!root) {
        LOGE(TAG, "Error parseando json Wi-Fi.");
        return false;
    }

    cJSON* networks = cJSON_GetObjectItem(root, "networks");
    int total = cJSON_GetArraySize(networks);
    int start_idx = cJSON_GetObjectItem(root, "last_used")->valueint;
    //write_system_log(TAG, "Indice: " + std::to_string(start_idx));
    LOGI(TAG, "Indice: %d", start_idx);
    if (start_idx >= total) start_idx = total - 1; // Seguridad

    bool conectado = false;

    // PARTE 1: Desde start_idx hacia atrás (3, 2, 1, 0)
    for (int i = start_idx; i >= 0; i--) {
        cJSON* item = cJSON_GetArrayItem(networks, i);
        const char* s = cJSON_GetObjectItem(item, "ssid")->valuestring;
        const char* p = cJSON_GetObjectItem(item, "pass")->valuestring;
        LOGI(TAG,"Probando index %d:  %s", i, s);
        //write_system_log(TAG, log_msg.c_str());
        //ESP_LOGI(TAG, "%s", log_msg.c_str() );
        if (conectar_wifi(s, p, 10000)) {
            save_wifi_network(s, p); // Actualiza last_used
            conectado = true; break;
        }
    }

    // PARTE 2: Si no conectó, probamos los que están "adelante" del índice (el resto)
    if (!conectado) {
        for (int i = total - 1; i > start_idx; i--) {
            cJSON* item = cJSON_GetArrayItem(networks, i);
            const char* s = cJSON_GetObjectItem(item, "ssid")->valuestring;
            const char* p = cJSON_GetObjectItem(item, "pass")->valuestring;
            LOGI(TAG,"Probando index %d: %s", i, s);
            //write_system_log(TAG, log_msg.c_str());
            //ESP_LOGI(TAG, "%s", log_msg.c_str());

            if (conectar_wifi(s, p, 10000)) {
                save_wifi_network(s, p);
                conectado = true; break;
            }
        }
    }

    cJSON_Delete(root);
    return conectado;
}


bool iniciar_proceso_conexion_maestra() {
    LOGI(TAG, "--- Iniciando Estrategia de Conexión Maestra ---");
    
    // NIVEL 1: Intentar con el historial JSON
    // Esta función ya recorre circularmente todas las redes guardadas
    LOGI(TAG, "Nivel 1: Probando historial JSON...");
    g_sistema_inicializando = true; // Bloqueamos al vigilante
    if (conectar_wifi_desde_json()) {
        LOGI(TAG, "Conexión exitosa via Nivel 1 (JSON).");
        g_sistema_inicializando = false; // Liberamos al vigilante para que cuide la conexión de ahora en más
        g_manual_wifi_connect = false; // Reiniciamos la bandera de conexión manual porque ya se conectó exitosamente
        return true; 
    }

    // NIVEL 2: Intentar con la red por defecto (Hardcoded)
    // Útil si el usuario reseteó el equipo o si es la primera vez que arranca
    LOGI(TAG, "Nivel 2: Probando credenciales default...");
    if (conectar_wifi(SSIDDEFAULT, PASSDEFAULT, 10000)) {
        LOGI(TAG, "Conexión exitosa via Nivel 2 (Default).");
        // Importante: Guardamos en JSON para que la próxima vez sea Nivel 1
        save_wifi_network(SSIDDEFAULT, PASSDEFAULT); 
        g_sistema_inicializando = false; // Liberamos al vigilante para que cuide la conexión de ahora en más
        g_manual_wifi_connect = false; // Reiniciamos la bandera de conexión manual porque ya se conectó exitosamente
        return true;
    }

    // NIVEL 3: El último recurso (NVS Nativa)
    // A veces el driver tiene grabada una red que no está en nuestro JSON
    LOGI(TAG, "Nivel 3: Probando recuperación desde NVS...");
    wifi_config_t nvs_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &nvs_cfg) == ESP_OK && strlen((char*)nvs_cfg.sta.ssid) > 0) {
        // Limpiamos los bits antes de intentar
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        
        if (esp_wifi_connect() == ESP_OK) {
            if (esperar_conexion(10000)) { // Usamos la función reusable que creamos
                LOGI(TAG, "Conexión exitosa via Nivel 3 (NVS).");
                save_wifi_network((char*)nvs_cfg.sta.ssid, (char*)nvs_cfg.sta.password);
                g_sistema_inicializando = false; // Liberamos al vigilante para que cuide la conexión de ahora en más
                g_manual_wifi_connect = false; // Reiniciamos la bandera de conexión manual porque ya se conectó exitosamente
                return true;
            }
        }
    }

    // NIVEL FINAL: Modo Punto de Acceso (AP)
    // Si llegamos aquí, nada funcionó. El equipo se vuelve un AP para ser configurado.
    LOGE(TAG, "FALLO TOTAL: Iniciando Modo AP de emergencia.");
    iniciar_modo_ap(MODO_AP_SSID, MODO_AP_PASS);
    g_sistema_inicializando = false;
    g_manual_wifi_connect = false; // Reiniciamos la bandera de conexión manual porque no se conectó
    return false; // Retornamos false porque no conectó como client (STA)
}
bool esperar_conexion(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,    // No limpiar los bits al salir (lo hacemos al iniciar cada intento)
            pdFALSE,    // Esperar a cualquier bit
            pdMS_TO_TICKS(timeout_ms));

    return (bits & WIFI_CONNECTED_BIT);
}

bool delete_wifi_network(const char* ssid_to_delete) {

    FILE* f = fopen(WIFI_JSON_FILE, "r");
    cJSON* root = NULL;

    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char* data = (char*)malloc(size + 1);
        fread(data, 1, size, f);
        data[size] = '\0';
        fclose(f);

        root = cJSON_Parse(data);
        free(data);
    }

    if (!root) {
        return false;   // No se pudo leer JSON
    }

    cJSON* networks = cJSON_GetObjectItem(root, "networks");
    if (!networks || !cJSON_IsArray(networks)) {
        cJSON_Delete(root);
        return false;
    }

    int last_used = cJSON_GetObjectItem(root, "last_used")->valueint;
    int total = cJSON_GetArraySize(networks);
    int found_index = -1;

    // Buscar el SSID
    for (int i = 0; i < total; i++) {
        cJSON* item = cJSON_GetArrayItem(networks, i);
        const char* ssid_json = cJSON_GetObjectItem(item, "ssid")->valuestring;

        if (strcmp(ssid_json, ssid_to_delete) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        cJSON_Delete(root);
        return false;   // No existe
    }

    // Borrar el elemento
    cJSON_DeleteItemFromArray(networks, found_index);

    // Ajustar last_used
    if (found_index == last_used) {
        last_used = 0;
    }
    else if (found_index < last_used) {
        last_used -= 1;
    }

    cJSON_ReplaceItemInObject(root, "last_used", cJSON_CreateNumber(last_used));

    // Guardar archivo
    char* nuevo_json = cJSON_Print(root);
    FILE* f_write = fopen(WIFI_JSON_FILE, "w");

    if (f_write) {
        fputs(nuevo_json, f_write);
        fclose(f_write);
    }

    LOGI(TAG, "JSON actualizado: %s", nuevo_json);

    free(nuevo_json);
    cJSON_Delete(root);

    return true;
}
