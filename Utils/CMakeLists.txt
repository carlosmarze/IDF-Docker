idf_component_register(
    SRCS  
        utils_ntp.cpp
        utils_files.cpp
        utils_webs.cpp
        utils_wifi.cpp
        utils_mqtt.cpp
        utils_time.cpp
        utils_http.cpp
        utils_cmd_dispatcher.cpp
        utils_cmd_processor.cpp
        utils_cmd_parser.cpp
        utils_cmd_set.cpp
        utils_ota_worker.cpp
        utils_mits.cpp
        utils_logger.cpp
        utils_config.cpp
        utils_cmd_webupdate.cpp
        utils_sched.cpp
        utils_events.cpp
          
    INCLUDE_DIRS "."
    PRIV_REQUIRES main
    REQUIRES 
        esp_wifi        
        esp_event       
        nvs_flash       
        esp_netif       
        freertos        
        log  
        cjson # idf.py add-dependency "espressif/cjson^1.0.0"
        littlefs
        esp_https_ota
        esp_http_client
        app_update
        mqtt # idf.py add-dependency espressif/esp-mqtt
        esp_http_server  
        mbedtls
        driver
        esp_driver_gpio  # AGREGA ESTA LÍNEA ESPECÍFICA
        esp_driver_gptimer # Si usas timers
        lwip
)
