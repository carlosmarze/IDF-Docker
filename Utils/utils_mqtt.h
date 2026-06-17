#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

    #ifdef __cplusplus
        extern "C"  {
    #endif

   #include "mqtt_client.h" // Incluye el tipo de dato
    // Función que inicia la tarea de MQTT y conexión
    //#define MQTT_BROKER_URI "mqtt://node02.myqtthub.com" 
 
    extern bool mqttconnStatus;
    extern esp_mqtt_client_handle_t client;
    //Root cerirtificate para HiveMQ Cloud (si usas ese broker)
    //ver en el browser quien es el firmante del certificado del broker, y bajar el certificado raíz correspondiente, lo convertís a PEM si es necesario, y lo incluís en el proyecto (ej: certs/hivemq_ca.pem) para luego usarlo acá. Si usas otro broker, bajate el certificado raíz de ese broker.
    
    extern char* mqtt_ca_cert;   // buffer dinámico cargado desde FS

    void mqtt_app_start(void); 
    void mqtt_app_startHIVE(void);
    // 🚨 NUEVA: Función para detener y destruir el cliente
    void mqtt_app_stop(void);
    void publish_mqtt(const char* topic, const char* data, int qos, int retain);
    bool mqtt_is_initialized(void); //para ver mqtt_initialized desde otros archivos, para no exponerlo y manejarlo internamente
    
    //void mqtt_watchdog_task(void *param); No se necesita acá porque es privada de utils_mqtt.cpp
    //las otras funciones se definen en utils_mqtt.cpp directamente y no van a ser usadas desde otro lado, son privadas, no hace falta declararlas
    void generar_topico_mqtt(const char *prefijo, int sensor_id, char *buffer_destino, size_t max_len) ; // 👈 Prefijo es A, K, Q

    #ifdef __cplusplus
        }
    #endif

#endif // MQTT_CLIENT_H