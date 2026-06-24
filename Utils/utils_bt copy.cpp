#include "utils_bt.h"
#include "esp_log.h"
#include <string.h>
#include "utils_cmd_processor.h"
#include "utils_cmd_dispatcher.h"

#include "esp_log.h"
#include <string.h>

static const char* TAG = "BLE_UART";

static ble_rx_callback_t g_rx_callback = nullptr;

// UUIDs del servicio NUS (Nordic UART Service)
#define NUS_SERVICE_UUID        0x6E400001B5A3F393E0A9E50E24DCCA9E
#define NUS_CHAR_UUID_RX        0x6E400002B5A3F393E0A9E50E24DCCA9E
#define NUS_CHAR_UUID_TX        0x6E400003B5A3F393E0A9E50E24DCCA9E

static uint16_t nus_service_handle = 0;
static uint16_t nus_rx_handle = 0;
static uint16_t nus_tx_handle = 0;
static esp_gatt_if_t gatts_if_global = 0;

// ---------------------------------------------------------------------------
//  BLE Handler
// ----
static void ble_rx_handler(const char* data, int len)
{
    char cmd[256];
    int n = len < 255 ? len : 255;
    memcpy(cmd, data, n);
    cmd[n] = '\0';

    process_commands(CMD_SRC_BT, cmd, ' ', '=', -1);
}

// ---------------------------------------------------------------------------
//  CALLBACKS
// ---------------------------------------------------------------------------

static void gatts_profile_event_handler(esp_gatts_cb_event_t event,
                                        esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATTS_REG_EVT");

            esp_ble_gap_set_device_name("ESP32S3-BLE-UART");

            // Publicidad mínima (después la mejoramos)
            uint8_t adv_raw[] = { 0x02, 0x01, 0x06 };
            esp_ble_gap_config_adv_data_raw(adv_raw, sizeof(adv_raw));

            // --- Crear servicio NUS ---
            esp_gatt_srvc_id_t service_id = {};

            service_id.is_primary = true;
            service_id.id.inst_id = 0;
            service_id.id.uuid.len = ESP_UUID_LEN_128;

            // UUID de 128 bits del servicio NUS (en formato little-endian)
            uint8_t nus_service_uuid[16] = {
                0x9E, 0xCA, 0xDC, 0x24,
                0x0E, 0xE5,
                0xA9, 0xE0,
                0x93, 0xF3,
                0xA3, 0xB5,
                0x01, 0x00, 0x40, 0x6E
            };

            memcpy(service_id.id.uuid.uuid.uuid128, nus_service_uuid, 16);

            esp_ble_gatts_create_service(gatts_if, &service_id, 10);

            break;
        }

        case ESP_GATTS_CREATE_EVT: {

            ESP_LOGI(TAG, "Servicio creado");
            nus_service_handle = param->create.service_handle;

            esp_ble_gatts_start_service(nus_service_handle);

            // UUID RX
            esp_bt_uuid_t rx_uuid = {};

            rx_uuid.len = ESP_UUID_LEN_128;

            uint8_t nus_rx_uuid[16] = {
                0x9E, 0xCA, 0xDC, 0x24,
                0x0E, 0xE5,
                0xA9, 0xE0,
                0x93, 0xF3,
                0xA3, 0xB5,
                0x02, 0x00, 0x40, 0x6E
            };

            memcpy(rx_uuid.uuid.uuid128, nus_rx_uuid, 16);

            esp_ble_gatts_add_char(
                nus_service_handle,
                &rx_uuid,
                ESP_GATT_PERM_WRITE,
                ESP_GATT_CHAR_PROP_BIT_WRITE,
                NULL, NULL
            );
        
            break;
        }
        case ESP_GATTS_ADD_CHAR_EVT: {
            if (param->add_char.char_uuid.uuid.uuid128[0] == (NUS_CHAR_UUID_RX & 0xFF)) {
                nus_rx_handle = param->add_char.attr_handle;
                ESP_LOGI(TAG, "RX handle: %d", nus_rx_handle);
            } else {
                nus_tx_handle = param->add_char.attr_handle;
                ESP_LOGI(TAG, "TX handle: %d", nus_tx_handle);
            }
            break;
        }
        case ESP_GATTS_WRITE_EVT:{

            ESP_LOGI(TAG, "WRITE_EVT (%d bytes)", param->write.len);

            if (g_rx_callback) {
                g_rx_callback((const char*)param->write.value, param->write.len);
            }
            break;
        }
        default: {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
//  INICIALIZACIÓN
// ---------------------------------------------------------------------------

void ble_uart_init()
{
    ESP_LOGI(TAG, "Inicializando BLE UART...");

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_profile_event_handler);
    esp_ble_gap_register_callback(NULL);

    esp_ble_gatts_app_register(0);
    ble_uart_set_rx_callback(ble_rx_handler);

    ESP_LOGI(TAG, "BLE UART listo.");
}

// ---------------------------------------------------------------------------
//  ENVÍO DE DATOS (Notify)
// ---------------------------------------------------------------------------

void ble_uart_send(const char* msg)
{
    if (nus_tx_handle == 0) {
        ESP_LOGW(TAG, "TX no inicializado");
        return;
    }

    esp_ble_gatts_send_indicate(
        gatts_if_global,
        0, // conn_id (0 si solo hay 1 conexión)
        nus_tx_handle,
        strlen(msg),
        (uint8_t*)msg,
        false
    );
}

// ---------------------------------------------------------------------------
//  REGISTRAR CALLBACK
// ---------------------------------------------------------------------------

void ble_uart_set_rx_callback(ble_rx_callback_t cb)
{
    g_rx_callback = cb;
}


