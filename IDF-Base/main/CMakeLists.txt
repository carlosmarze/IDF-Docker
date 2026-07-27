# idf_component_register(SRCS "pr_hacomm.cpp" "pr_ds18b20.cpp" "pr_onewiresearch.cpp" "pr_onewire.cpp" "config_proyecto.cpp" "cmd_Led_borrar.cpp" "project_tasks.cpp" "sched_tasks.cpp" "main.cpp"
#                    INCLUDE_DIRS ".")
#target_link_libraries(${COMPONENT_TARGET} PRIVATE esp_wifi esp_event nvs_flash lwip log mqtt)
# target_link_libraries(${COMPONENT_TARGET} PRIVATE esp_wifi esp_event nvs_flash lwip log mqtt littlefs)
idf_component_register(
    SRCS 
        "main.cpp"
        "sched_tasks.cpp"
        "project_tasks.cpp"
        "pr_onewire.cpp"
        "pr_hacomm.cpp"
        #"onewiresearch.cpp"
        #"pr_ds18b20.cpp"
        "config_proyecto.cpp"
        INCLUDE_DIRS "."
        #EMBED_TXTFILES "certs/hivemq_ca.pem" Lo leemos desde FS, no lo cargamos en el firmware
    
    # AGREGAR ESTA LÍNEA CON TODOS LOS COMPONENTES NECESARIOS
    PRIV_REQUIRES 
        "freertos"
        "esp_wifi"
        "esp_event"
        "nvs_flash"
        "lwip"
        "log"
        "mqtt"
        "littlefs"
        "cjson"
        "bt"
        #
        #"esp_http_client" 
        #"app_update" 
        #"esp_https_ota"
        #"nvs_flash"
        #"esp_ota_ops" 
        #"esp_https_ota"
        #"nvs"
        "Utils"        # <--- ¡Se añade el componente externo por su nombre!
)

# ELIMINA o COMENTA la línea target_link_libraries que está fallando
# target_link_libraries(${COMPONENT_TARGET} PRIVATE esp_wifi esp_event nvs_flash lwip log mqtt littlefs)


