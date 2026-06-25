#pragma once

#include <stdint.h>
#include <functional>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa BLE + servicio UART
void ble_uart_init();

// Envía datos por BLE (Notify)
void ble_uart_send(const char* msg);

//Stop y restart 
void ble_stop_for_ota();
void ble_restart_after_ota();
bool ble_status();
// Callback para recibir datos
typedef void (*ble_rx_callback_t)(const char* data, int len);
void ble_uart_set_rx_callback(ble_rx_callback_t cb);

#ifdef __cplusplus
}
#endif
