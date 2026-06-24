#ifndef UTILS_BT_H
#define UTILS_BT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
//  TIPOS DE DATOS
// ---------------------------------------------------------------------------

/**
 * @brief Definición del callback para recibir datos por BLE.
 * 
 * @param data Puntero a los datos recibidos (no necesariamente terminados en null).
 * @param len  Longitud de los datos recibidos en bytes.
 */
typedef void (*ble_rx_callback_t)(const char* data, int len);

// ---------------------------------------------------------------------------
//  FUNCIONES PÚBLICAS
// ---------------------------------------------------------------------------

/**
 * @brief Inicializa el stack BLE (NimBLE), configura el servicio NUS 
 *        y comienza a publicitar el dispositivo.
 */
void ble_uart_init(void);

/**
 * @brief Envía una cadena de texto a través de la característica TX (Notify).
 * 
 * @param msg Cadena de texto terminada en null ('\0') a enviar.
 * 
 * @note Si no hay ningún cliente BLE conectado, la función no hará nada 
 *       y mostrará un warning en el log.
 */
void ble_uart_send(const char* msg);

/**
 * @brief Registra la función que será llamada cada vez que se reciban 
 *        datos en la característica RX.
 * 
 * @param cb Puntero a la función callback.
 */
void ble_uart_set_rx_callback(ble_rx_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif // UTILS_BT_H