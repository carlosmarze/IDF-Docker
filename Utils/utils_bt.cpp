#include "utils_bt.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "utils_cmd_processor.h"

static const char* TAG = "BLE_UART";

// ---------------------------------------------------------------------------
//  VARIABLES DE ESTADO
// ---------------------------------------------------------------------------
static bool ble_is_running = false;
static uint16_t nus_rx_handle = 0;
static uint16_t nus_tx_handle = 0;
static uint16_t conn_handle_global = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_addr_type;

static ble_rx_callback_t g_rx_callback = nullptr;

// ---------------------------------------------------------------------------
//  UUIDs NUS en formato LITTLE-ENDIAN (obligatorio para NimBLE)
// ---------------------------------------------------------------------------
static const ble_uuid128_t NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

// ---------------------------------------------------------------------------
//  CALLBACK DE REGISTRO GATT
// ---------------------------------------------------------------------------
static void ble_gatts_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            ESP_LOGI(TAG, "Servicio registrado: handle=%d", ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGI(TAG, "Característica registrada: def_handle=%d val_handle=%d",
                     ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGI(TAG, "Descriptor registrado: handle=%d", ctxt->dsc.handle);
            break;
    }
}

// ---------------------------------------------------------------------------
//  HANDLER DE ACCESO GATT
// ---------------------------------------------------------------------------
static int ble_uart_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Escribir en RX
    if (ble_uuid_cmp(ctxt->chr->uuid, (const ble_uuid_t *)&NUS_RX_UUID) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            const uint8_t* data = ctxt->om->om_data;
            int len = ctxt->om->om_len;

            ESP_LOGI(TAG, "RX: %d bytes", len);

            if (g_rx_callback) {
                g_rx_callback((const char*)data, len);
            }
            return 0;
        }
    }

    // Leer TX (no permitido en NUS estándar)
    if (ble_uuid_cmp(ctxt->chr->uuid, (const ble_uuid_t *)&NUS_TX_UUID) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// ---------------------------------------------------------------------------
//  HELPER PARA INICIALIZAR ESTRUCTURAS (compatible con C++)
// ---------------------------------------------------------------------------
static void init_chr(struct ble_gatt_chr_def *chr,
                     const ble_uuid_t *uuid,
                     ble_gatt_access_fn *access_cb,
                     uint16_t *val_handle,
                     uint16_t flags)
{
    memset(chr, 0, sizeof(*chr));
    chr->uuid       = uuid;
    chr->access_cb  = access_cb;
    chr->arg        = NULL;
    chr->val_handle = val_handle;
    chr->flags      = flags;
    // Campos opcionales ya están a 0/NULL por el memset
}

// Arrays estáticos (se inicializan en ble_uart_init)
static struct ble_gatt_chr_def gatt_uart_chars[3];
static struct ble_gatt_svc_def gatt_uart_svc[2];

// ---------------------------------------------------------------------------
//  GAP EVENTOS
// ---------------------------------------------------------------------------
static void start_advertising(void);

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Conectado");
                conn_handle_global = event->connect.conn_handle;
            } else {
                ESP_LOGE(TAG, "Error de conexión: %d", event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Desconectado, reiniciando advertising");
            conn_handle_global = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe: conn=%d, notify=%d",
                     event->subscribe.conn_handle, event->subscribe.cur_notify);
            break;

        default:
            break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name = "ESP32-NUS";
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error en adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error en ble_gap_adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising iniciado");
    }
}

// ---------------------------------------------------------------------------
//  CALLBACKS DEL HOST (sin lambdas)
// ---------------------------------------------------------------------------
static void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "BLE sync OK");
    ble_hs_id_infer_auto(0, &own_addr_type);
    start_advertising();
}

static void ble_app_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset: reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task iniciado");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
//  HANDLER INTERNO QUE LLAMA A process_commands()
// ---------------------------------------------------------------------------
static void ble_rx_handler(const char* data, int len)
{
    char cmd[256];
    int n = (len < 255) ? len : 255;
    memcpy(cmd, data, n);
    cmd[n] = '\0';

    process_commands(CMD_SRC_BT, cmd, ' ', '=', -1);
}

// ---------------------------------------------------------------------------
//  INICIALIZACIÓN
// ---------------------------------------------------------------------------
void ble_uart_init()
{
    //ESP_LOGI(TAG, "Inicializando NimBLE UART...");
    ESP_LOGI(TAG, "Iniciando NimBLE UART. Heap libre: %d", esp_get_free_heap_size());
    // 1) Construir tablas GATT (C++ friendly)
    init_chr(&gatt_uart_chars[0],
             (const ble_uuid_t *)&NUS_RX_UUID,
             ble_uart_access_cb,
             &nus_rx_handle,
             BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP);

    init_chr(&gatt_uart_chars[1],
             (const ble_uuid_t *)&NUS_TX_UUID,
             ble_uart_access_cb,
             &nus_tx_handle,
             BLE_GATT_CHR_F_NOTIFY);

    memset(&gatt_uart_chars[2], 0, sizeof(gatt_uart_chars[2])); // terminador

    memset(&gatt_uart_svc[0], 0, sizeof(gatt_uart_svc[0]));
    gatt_uart_svc[0].type            = BLE_GATT_SVC_TYPE_PRIMARY;
    gatt_uart_svc[0].uuid            = (const ble_uuid_t *)&NUS_SERVICE_UUID;
    gatt_uart_svc[0].includes        = NULL;
    gatt_uart_svc[0].characteristics = gatt_uart_chars;

    memset(&gatt_uart_svc[1], 0, sizeof(gatt_uart_svc[1])); // terminador

    // 2) Inicializar stack NimBLE
    nimble_port_init();

    // 3) Servicios básicos
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // 4) Callbacks del host
    ble_hs_cfg.gatts_register_cb = ble_gatts_register_cb;
    ble_hs_cfg.reset_cb          = ble_app_on_reset;
    ble_hs_cfg.sync_cb           = ble_app_on_sync;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    // 5) Nombre del dispositivo
    ble_svc_gap_device_name_set("ESP32-NUS");

    // 6) Registrar servicio NUS
    int rc = ble_gatts_count_cfg(gatt_uart_svc);
    ESP_LOGI(TAG, "count_cfg rc=%d", rc);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error en count_cfg");
        return;
    }

    rc = ble_gatts_add_svcs(gatt_uart_svc);
    ESP_LOGI(TAG, "add_svcs rc=%d", rc);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error en add_svcs");
        return;
    }

    // 7) Callback de recepción de la aplicación
    ble_uart_set_rx_callback(ble_rx_handler);

    // 8) Arrancar tarea del host
    nimble_port_freertos_init(ble_host_task);
    ble_is_running = true;
    ESP_LOGI(TAG, "NimBLE inicializado");
    ESP_LOGI(TAG, "Luegp de inicio NimBLE. Heap libre: %d", esp_get_free_heap_size());
}


// ---------------------------------------------------------------------------
//  ENVÍO DE DATOS (Notify) - Dividido en chunks de 100 bytes
// ---------------------------------------------------------------------------
void ble_uart_send(const char* msg)
{
    if (conn_handle_global == BLE_HS_CONN_HANDLE_NONE || !nus_tx_handle || !ble_is_running) {
        ESP_LOGW(TAG, "No conectado o TX no inicializado");
        return;
    }

    int total_len = strlen(msg);
    const int CHUNK_SIZE = 100; // Tamaño seguro para la mayoría de clientes BLE
    int offset = 0;

    ESP_LOGI(TAG, "Enviando notify: %d bytes en chunks de %d", total_len, CHUNK_SIZE);

    while (offset < total_len) {
        int chunk_len = (total_len - offset) < CHUNK_SIZE ? (total_len - offset) : CHUNK_SIZE;
        
        struct os_mbuf *om = ble_hs_mbuf_from_flat(msg + offset, chunk_len);
        if (!om) {
            ESP_LOGE(TAG, "Error asignando mbuf en offset %d", offset);
            return;
        }

        int rc = ble_gatts_notify_custom(conn_handle_global, nus_tx_handle, om);
        if (rc != 0) {
            ESP_LOGE(TAG, "Error en notify rc=%d (offset=%d)", rc, offset);
            return;
        }

        offset += chunk_len;
        
        // Pequeña pausa para evitar saturar el stack BLE
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Notify completado: %d bytes enviados", total_len);
}

// ---------------------------------------------------------------------------
//  REGISTRAR CALLBACK DE RECEPCIÓN
// ---------------------------------------------------------------------------
void ble_uart_set_rx_callback(ble_rx_callback_t cb)
{
    g_rx_callback = cb;
}
// ---------------------------------------------------------------------------
//  Stop BLE antes de OTA y reinicio posterior, porque si no, da error de SSL
// ---------------------------------------------------------------------------


void ble_stop_for_ota()
{
    if (!ble_is_running) {
        ESP_LOGW(TAG, "BLE ya está detenido");
        return;
    }
    
    ESP_LOGI(TAG, "Deteniendo NimBLE para OTA...");
    
    // 1) Detener advertising
    ble_gap_adv_stop();
    
    // 2) Desconectar cliente si está conectado
    if (conn_handle_global != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle_global, BLE_ERR_REM_USER_CONN_TERM);
        conn_handle_global = BLE_HS_CONN_HANDLE_NONE;
        vTaskDelay(pdMS_TO_TICKS(100)); // Esperar desconexión
    }
    
    // 3) Detener el host (la tarea termina)
    nimble_port_stop();
    
    // 4) Esperar a que la tarea del host termine realmente
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 5) Liberar recursos
    nimble_port_deinit();
    
    ble_is_running = false;
    
    ESP_LOGI(TAG, "NimBLE detenido. Heap libre: %u bytes", esp_get_free_heap_size());
}

void ble_restart_after_ota()
{
    if (ble_is_running) {
        ESP_LOGW(TAG, "BLE ya está corriendo");
        return;
    }
    
    ESP_LOGI(TAG, "Reiniciando NimBLE después de OTA...");
    
    // Resetear handles (se regenerarán al registrar servicios)
    nus_rx_handle = 0;
    nus_tx_handle = 0;
    
    // Llamar a la misma función de inicialización
    ble_uart_init();
    
    ble_is_running = true;
    
    ESP_LOGI(TAG, "NimBLE reiniciado. Heap libre: %u bytes", esp_get_free_heap_size());
}

bool ble_status() {
    return ble_is_running;
}