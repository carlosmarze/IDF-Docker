flowchart TD

    %% ============================
    %% APP_MAIN
    %% ============================
    A[app_main()] --> B[xTaskCreate(app_task)]

    %% ============================
    %% APP_TASK - FASE 0
    %% ============================
    B --> C0[FASE 0: Infraestructura base\n- littlefs_init()\n- init_logger_system()\n- init_system_events()\n- setenv(TZ)\n- log versión]

    %% ============================
    %% FASE 1
    %% ============================
    C0 --> C1[FASE 1: NVS + wifi.json\n- nvs_flash_init()\n- auto-saneado wifi.json]

    %% ============================
    %% FASE 2
    %% ============================
    C1 --> C2[FASE 2: Red + WiFi\n- esp_netif_init()\n- event_loop_create()\n- wifi_hardware_init()\n- xTaskCreate(wifi_check)\n- xTaskCreate(wifi_connect)]

    %% ============================
    %% FASE 3
    %% ============================
    C2 --> C3[FASE 3: Sistema de comandos\n- crear command_queue\n- dispatcher.start()\n- registrar comandos\n- xTaskCreate(dispatcher_task)\n- cargar_config_desde_file()]

    %% ============================
    %% FASE 4
    %% ============================
    C3 --> C4[FASE 4: Conexión maestra\n- iniciar_proceso_conexion_maestra()]

    %% ============================
    %% FASE 5
    %% ============================
    C4 --> C5[FASE 5: NTP si hay WiFi\n- ntp_client_init()\n- esperar sincronización]

    %% ============================
    %% FASE 6
    %% ============================
    C5 --> C6[FASE 6: OTA\n- preparar URL\n- ota_worker_start()\n- si hay WiFi → process_commands("ota")]

    %% ============================
    %% FASE 7
    %% ============================
    C6 --> C7[FASE 7: Scheduler + Servicios\n- miTS_init()\n- registrar tareas 60/3600\n- esperar g_ota_en_progreso == false\n- start_scheduler()\n- xTaskCreate(service_starter_task)]

    %% ============================
    %% SERVICE STARTER TASK
    %% ============================
    C7 --> S0[service_starter_task()]
    S0 --> S1[Esperar fin de OTA o timeout]
    S1 --> S2[Si WiFi listo y no OTA:\n- iniciar MQTT\n- iniciar WebServer\n- crear tareas auxiliares\n- ejecutar FIRST RUN]

    %% ============================
    %% LOOP PRINCIPAL
    %% ============================
    C7 --> L[Loop principal\n- cada 60s log heap\n- tareas especiales]

