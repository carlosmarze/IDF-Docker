#include "utils_bt.h"
#include "esp_log.h"
#include <string.h>
#include "utils_cmd_processor.h"
#include "utils_cmd_dispatcher.h"

// --- Headers de NimBLE (Compatibles con ESP-IDF v5 y v6) ---
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

// El header principal que engloba GAP, GATTS, mbufs, etc.
#include "host/ble_hs.h" 
#include "host/ble_uuid.h"
#include "host/util/util.h"

// Servicios por defecto de NimBLE
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h" 

static const char* TAG = "BLE_UART";

static ble_rx_callback_t g_rx_callback = nullptr;

// Variables de estado de NimBLE
static uint16_t nus_rx_handle = 0;
static uint16_t nus_tx_handle = 0;
static uint16_t conn_handle_global = BLE_HS_CONN_HANDLE_NONE; // 0xFFFF
static uint8_t own_addr_type;

// ---------------------------------------------------------------------------
//  UUIDs del servicio NUS (Nordic UART Service)
//  En NimBLE, los UUIDs de 128 bits se definen en formato Little-Endian
// ---------------------------------------------------------------------------
static const ble_uuid128_t gatt_nus_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E
);

static const ble_uuid128_t gatt_nus_rx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E
);

static const ble_uuid128_t gatt_nus_tx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E
);

// ---------------------------------------------------------------------------
//  BLE Handler (Sin cambios)
// ---------------------------------------------------------------------------
static void ble_rx_handler(const char* data, int len)
{
    char cmd[256];
    int n = len < 255 ? len : 255;
    memcpy(cmd, data, n);
    cmd[n] = '\0';

    process_commands(CMD_SRC_BT, cmd, ' ', '=', -1);
}

// ---------------------------------------------------------------------------
//  CALLBACK DE ACCESO A CARACTERÍSTICAS (GATTS)
// ---------------------------------------------------------------------------
static int gatt_svr_chr_access_nus(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Si escriben en la característica RX
    if (ble_uuid_cmp(ctxt->chr->uuid, (ble_uuid_t *)&gatt_nus_rx_uuid) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            ESP_LOGI(TAG, "RX recibido: %d bytes", ctxt->om->om_len);
            if (g_rx_callback) {
                g_rx_callback((const char *)ctxt->om->om_data, ctxt->om->om_len);
            }
            return 0;
        }
    }
    
    // Si leen la característica TX (No permitido en NUS estándar)
    if (ble_uuid_cmp(ctxt->chr->uuid, (ble_uuid_t *)&gatt_nus_tx_uuid) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return BLE_ATT_ERR_READ_NOT_PERMITTED;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// ---------------------------------------------------------------------------
//  DEFINICIÓN ESTÁTICA DEL SERVICIO Y CARACTERÍSTICAS (NIMBLE)
// ---------------------------------------------------------------------------
static const struct ble_gatt_chr_def gatt_nus_chrs[] = {
    {
        .uuid = (ble_uuid_t *)&gatt_nus_rx_uuid,
        .access_cb = gatt_svr_chr_access_nus,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &nus_rx_handle,
    },
    {
        .uuid = (ble_uuid_t *)&gatt_nus_tx_uuid,
        .access_cb = gatt_svr_chr_access_nus,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &nus_tx_handle,
    },
    {
        0, // Fin de las características
    }
};

static const struct ble_gatt_svc_def gatt_nus_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (ble_uuid_t *)&gatt_nus_svc_uuid,
        .characteristics = gatt_nus_chrs,
    },
    {
        0, // Fin de los servicios
    }
};

// ---------------------------------------------------------------------------
//  PUBLICIDAD Y EVENTOS GAP
// ---------------------------------------------------------------------------
static void start_advertising(void);

static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Conexión establecida");
            conn_handle_global = event->connect.conn_handle;
        } else {
            ESP_LOGE(TAG, "Error de conexión, status=%d", event->connect.status);
            start_advertising(); // Reintentar publicidad
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Desconectado, reason=%d", event->disconnect.reason);
        conn_handle_global = BLE_HS_CONN_HANDLE_NONE;
        start_advertising(); // Volver a publicitar
        break;
        
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event, conn_handle=%d, notify=%d", 
                 event->subscribe.conn_handle, event->subscribe.cur_notify);
        break;

    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)"ESP32S3-BLE-UART";
    fields.name_len = strlen((char*)fields.name);
    fields.name_is_complete = 1;
    
    // Incluir UUID del servicio en la publicidad
    fields.uuids128 = (ble_uuid128_t[]){ gatt_nus_svc_uuid };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error configurando publicidad; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error iniciando publicidad; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Publicidad iniciada");
}

// ---------------------------------------------------------------------------
//  CALLBACKS DE ESTADO DEL HOST (SYNC / RESET)
// ---------------------------------------------------------------------------
static void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0, &own_addr_type);
    start_advertising();
}

static void ble_app_on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

// ---------------------------------------------------------------------------
//  TAREA DEL HOST NIMBLE
// ---------------------------------------------------------------------------
static void ble_uart_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task iniciado");
    nimble_port_run(); // Esto corre hasta que se llama a nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------------------
//  INICIALIZACIÓN
// ---------------------------------------------------------------------------
void ble_uart_init()
{
    ESP_LOGI(TAG, "Inicializando BLE UART con NimBLE...");

    // 1. Inicializar el puerto NimBLE
    nimble_port_init();

    // 2. Configurar callbacks del host
    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    //
    // 4. Inicializar servicios básicos de NimBLE (GAP y GATT por defecto)
    ble_svc_gap_init();
    ble_svc_gatt_init(); // <--- Añade esto para evitar problemas de inicialización GATT

    // 3. Configurar nombre del dispositivo
    ble_svc_gap_device_name_set("ESP32S3-BLE-UART");

    // 4. Registrar el servicio NUS estáticamente
    int rc = ble_gatts_count_cfg(gatt_nus_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_nus_svcs);
    assert(rc == 0);

    // 5. Iniciar la tarea FreeRTOS que maneja el host NimBLE
    nimble_port_freertos_init(ble_uart_host_task);
    
    // 6. Registrar callback de recepción de tu aplicación
    ble_uart_set_rx_callback(ble_rx_handler);

    ESP_LOGI(TAG, "BLE UART listo.");
}

// ---------------------------------------------------------------------------
//  ENVÍO DE DATOS (Notify)
// ---------------------------------------------------------------------------
void ble_uart_send(const char* msg)
{
    if (conn_handle_global == BLE_HS_CONN_HANDLE_NONE || nus_tx_handle == 0) {
        ESP_LOGW(TAG, "No conectado o TX no inicializado");
        return;
    }

    int len = strlen(msg);
    
    // En NimBLE, los datos se envían usando "mbufs" (memory buffers)
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, len);
    if (!om) {
        ESP_LOGE(TAG, "Error asignando mbuf para envío");
        return;
    }

    // Enviar notificación personalizada
    ble_gatts_notify_custom(conn_handle_global, nus_tx_handle, om);
}

// ---------------------------------------------------------------------------
//  REGISTRAR CALLBACK
// ---------------------------------------------------------------------------
void ble_uart_set_rx_callback(ble_rx_callback_t cb)
{
    g_rx_callback = cb;
}