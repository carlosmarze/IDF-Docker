#include "utils_bt.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

//#include "utils_logger.h"
#include "utils_cmd_processor.h"

static const char* TAG = "BLE_UART";

static uint16_t nus_rx_handle;
static uint16_t nus_tx_handle;

static ble_rx_callback_t g_rx_callback = nullptr;

// UUIDs NUS (Nordic UART Service)
static const ble_uuid128_t NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x6E,0x40,0x00,0x01,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x6E,0x40,0x00,0x02,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x6E,0x40,0x00,0x03,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);


// ---------------------------------------------------------------------------
//  HANDLERS
// ---------------------------------------------------------------------------

static int ble_uart_rx_handler(uint16_t conn_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        const uint8_t* data = ctxt->om->om_data;
        int len = ctxt->om->om_len;

        if (g_rx_callback) {
            g_rx_callback((const char*)data, len);
        }
    }
    return 0;
}

static int ble_uart_tx_handler(uint16_t conn_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg)
{
    return 0; // TX es solo notify
}


// ---------------------------------------------------------------------------
//  DEFINICIÓN DEL SERVICIO NUS (C++ COMPATIBLE)
// ---------------------------------------------------------------------------

static const struct ble_gatt_svc_def gatt_uart_svc[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,      // type
        &NUS_SERVICE_UUID.u,            // uuid
        NULL,                           // includes
        (struct ble_gatt_chr_def[]) {
            {
                &NUS_RX_UUID.u,          // uuid
                ble_uart_rx_handler,     // access_cb
                NULL,                    // arg
                NULL,                    // descriptors
                BLE_GATT_CHR_F_WRITE,    // flags
                0,                       // min_key_size
                &nus_rx_handle,          // val_handle
                NULL                     // cpfd
            },
            {
                &NUS_TX_UUID.u,          // uuid
                ble_uart_tx_handler,     // access_cb
                NULL,                    // arg
                NULL,                    // descriptors
                BLE_GATT_CHR_F_NOTIFY,   // flags
                0,                       // min_key_size
                &nus_tx_handle,          // val_handle
                NULL                     // cpfd
            },
            { 0 } // terminador de características
        }
    },
    { 0 } // terminador de servicios
};



// ---------------------------------------------------------------------------
//  GAP EVENTOS
// ---------------------------------------------------------------------------

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT: {
            ESP_LOGI(TAG, "Conectado");
            break;
        }

        case BLE_GAP_EVENT_DISCONNECT:{
        
            ESP_LOGI(TAG, "Desconectado, reiniciando advertising");

            // Declaración válida en C++
            struct ble_gap_adv_params adv_params;
            memset(&adv_params, 0, sizeof(adv_params));

            adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
            adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

            ble_gap_adv_start(
                0,                  // own address type
                NULL,               // no peer address
                BLE_HS_FOREVER,     // duración
                &adv_params,        // parámetros válidos
                gap_event_handler,  // callback GAP
                NULL                // arg
            );
            break;
        }
    }

    return 0;
}



// ---------------------------------------------------------------------------
//  INICIALIZACIÓN
// ---------------------------------------------------------------------------
static void ble_rx_handler(const char* data, int len)
{
    char cmd[256];

    int n = (len < 255) ? len : 255;
    memcpy(cmd, data, n);
    cmd[n] = '\0';

    // Llamada directa a tu parser
    process_commands(CMD_SRC_BT, cmd, ' ', '=', -1);
}


void ble_uart_init()
{
    ESP_LOGI(TAG, "Inicializando NimBLE UART...");

    nimble_port_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_uart_svc);
    if (rc != 0) ESP_LOGE(TAG, "count_cfg error %d", rc);

    rc = ble_gatts_add_svcs(gatt_uart_svc);
    if (rc != 0) ESP_LOGE(TAG, "add_svcs error %d", rc);

    ble_hs_cfg.sync_cb = []() {
        ESP_LOGI(TAG, "BLE sync OK");

        struct ble_gap_adv_params adv_params;
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

        ble_gap_adv_start(0, NULL, BLE_HS_FOREVER,
                          &adv_params,
                          gap_event_handler, NULL);
    };
    // REGISTRO AUTOMÁTICO DEL CALLBACK
    ble_uart_set_rx_callback(ble_rx_handler);

    nimble_port_freertos_init([](void*) { nimble_port_run(); });
}


// ---------------------------------------------------------------------------
//  ENVÍO DE DATOS (Notify)
// ---------------------------------------------------------------------------

void ble_uart_send(const char* msg)
{
    if (!nus_tx_handle) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    ble_gatts_notify_custom(0, nus_tx_handle, om);
}


// ---------------------------------------------------------------------------
//  CALLBACK DE RECEPCIÓN
// ---------------------------------------------------------------------------

void ble_uart_set_rx_callback(ble_rx_callback_t cb)
{
    g_rx_callback = cb;
}

