#include "sched_tasks.h"
#include "project_tasks.h"
#include "project_tasks.h"     // tus comandos
#include "pr_onewire.h"
//#include "pr_ds18b20.h"
//#include "pr_onewiresearch.h"
#include "config_proyecto.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include <string>
#include <cstring>

#include "utils_http.h"
#include "utils_wifi.h"
#include "utils_test.h"
#include "utils_mits.h"
#include "utils_time.h"
#include "utils_mits.h"
#include "utils_cmd_dispatcher.h"
#include "utils_cmd_processor.h"
#include "utils_config.h"
#include "utils_logger.h"
#include "MisVariablesProyecto.h"
#include "project_tasks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string>

static const char* TAG = "PROJECT_TASKS";
/*
class LedCommandB : public Command {
public:
    const char* name() const override { return "set_led"; }
    const char* usage() const override { return "[nroLed=On/Off] - TBI Setea el estado del LED nroLed"; } // <-- Añadir
    int minArgs() const override { return 2; }
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
    */
class LedOnCommand : public Command {
public:
    const char* name() const override { return "ledon"; }
    const char* usage() const override { return "[pin] - Enciende el LED en el pin especificado"; }
    int minArgs() const override { return 1; }
    bool positionalArgs() const override { return true; }   // si tu base lo requiere
    std::string execute(cmd_source_t src, const std::vector<std::string>& args) override {
        int pin = atoi(args[0].c_str());

        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pin, 1);

        return "LED encendido en pin " + std::to_string(pin);
    }
};
class LedSetCommand : public Command {
public:
    const char* name() const override { return "setled"; }
    const char* usage() const override { return "[nroLed=On/Off] - TBI Setea el estado del LED nroLed"; } // <-- Añadir
    int minArgs() const override { return 2; }
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
        const std::string &mode = args[0];
        int gpio = std::stoi(args[1]);
        bool on = (strcasecmp(mode.c_str(), "on") == 0);
        ESP_LOGI(TAG, "LED gpio=%d -> %s", gpio, on ? "ON" : "OFF");
        return "LED set";
    }
};

class PinCommand : public Command {
public:
    const char* name() const override { return "setpin"; }
    const char* usage() const override { return "[nroPin] [On/Off] - Setea pin OUT y el estado del pin nroPin"; } // <-- Añadir
    int minArgs() const override { return 2; }
    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {
        const std::string &mode = args[1];
        int gpio = std::stoi(args[0]);
        bool on = (strcasecmp(mode.c_str(), "on") == 0);
        gpio_reset_pin((gpio_num_t)gpio);
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)gpio, on ? 1 : 0); //habría que chequear si está configurado como salida, o configurarlo como salida antes de setear el nivel, para evitar errores. O directamente configurar como salida cada vez que se ejecute el comando, aunque eso no es lo más eficiente.
        std::string msg = "Pin gpio=" + std::to_string(gpio) + " -> " + (on ? "ON" : "OFF");
        ESP_LOGI(TAG, "%s", msg.c_str());
        return msg;
    }
};
/***********************************************************************************
 * Comando para leer la temperatura de sensores DS18B20 conectados a un pin específico.
 * Ejecución asincrónica: el comando se encola y la tarea de OneWire lo procesa.
 * Opciones:
        //tempscan scan - busca todos los sensores DS18B20 en el pin por defecto (GPIO4). Se puede correr una sola vez al inicio o cuando se quiere re escanear los sensores
        //tempscan - escanea todos los sensores DS18B20 en el pin por defecto (GPIO4)
        //tempscan id=2801A3B91204007C
        //tempscan 
        //tempscan fast sin 750ms de espera
        //tempscan raw devuelve scratchpad en hex además de la temperatura
        //tempscan pin=5 usa otro GPIO
        //tempscan read Lee el json último generado
**********************************************************************************/        
class TempScanCommand : public Command {
public:
    const char* name() const override { return "tempscan"; }
    const char* usage() const override {
        return "Lee async sensores DS18B20. Opciones: fast, raw, id=[rom], pin=[gpio], rescan, read";
    }
    int minArgs() const override { return 0; }

    std::string execute(cmd_source_t, const std::vector<std::string>& args) override {

        bool fast = false;
        bool raw = false;
        bool do_rescan = false;
        bool pin_changed = false;
        std::string target_id;

        // -----------------------------
        // PARSE ARGUMENTOS
        // -----------------------------
        for (const auto &a : args) {
            
            if (strcasecmp(a.c_str(), "read") == 0) {
                OneWireCommand cmd;
                cmd.type = OneWireCmdType::READ_ALL;

                if (xQueueSend(q_onewire, &cmd, 0) != pdTRUE)
                    return "{\"error\":\"cola onewire llena\"}";
                    
                const int max_wait_ms = 1000;   // 1 segundo
                const int step_ms = 50;         // chequeo cada 50 ms
                int waited = 0;
                while (!is_onewire_idle() && waited < max_wait_ms) {
                    vTaskDelay(pdMS_TO_TICKS(step_ms));
                    waited += step_ms;
                }
                if(!is_onewire_idle()) {
                    return "{\"error\":\"OneWire ocupado, Reintentar\"}";
                }
                printf("OneWire idle, devolviendo último json generado: %s", tempjson.c_str());
                return tempjson; //devuelvo el último json generado
            }
                
            if (strcasecmp(a.c_str(), "fast") == 0)
                fast = true;

            else if (strcasecmp(a.c_str(), "raw") == 0)
                raw = true;

            else if (strcasecmp(a.c_str(), "rescan") == 0)
                do_rescan = true;

            else if (a.rfind("pin=", 0) == 0) {
                int new_pin = std::stoi(a.substr(4));
                g_onewire_pin = new_pin;
                pin_changed = true;
            }

            else if (a.rfind("id=", 0) == 0)
                target_id = a.substr(3);
        }

        // -----------------------------
        // SI CAMBIÓ EL PIN → RESCAN AUTOMÁTICO
        // -----------------------------
        if (pin_changed) {
            g_sensors.clear();
            onewire_init = false;
            do_rescan = true;
        }

        // -----------------------------
        // 1) SI HAY RESCAN → ENCOLAR RESCAN
        // -----------------------------
        if (do_rescan) {
            OneWireCommand rescan_cmd;
            rescan_cmd.type = OneWireCmdType::RESCAN;

            if (xQueueSend(q_onewire, &rescan_cmd, 0) != pdTRUE)
                return "{\"error\":\"cola onewire llena (rescan)\"}";
        }

        // -----------------------------
        // 2) ARMAR EL COMANDO DE LECTURA
        // -----------------------------
        OneWireCommand cmd;

        if (!target_id.empty()) {
            cmd.type = OneWireCmdType::READ_ONE;
            cmd.id = target_id;
        }
        else if (fast) {
            cmd.type = OneWireCmdType::READ_FAST;
        }
        else if (raw) {
            cmd.type = OneWireCmdType::READ_RAW;
        }
        else {
            cmd.type = OneWireCmdType::READ_ALL;
        }

        // -----------------------------
        // 3) ENCOLAR EL COMANDO DE LECTURA
        // -----------------------------
        if (xQueueSend(q_onewire, &cmd, 0) != pdTRUE)
            return "{\"error\":\"cola onewire llena (lectura)\"}";

        // -----------------------------
        // RESPUESTA NO BLOQUEANTE
        // -----------------------------
        return "{\"status\":\"queued\"}";
    }
};



static void project_tasks(void* arg)
{
    LOGI(TAG, "Tareas del proyecto iniciadas"); 
    uint32_t seconds = 0;
    //verificar acá lo que necesite la tarea, por ejemplo si necesita esperar a que el WiFi esté conectado, o a que el NTP esté sincronizado, o a que algún recurso esté listo, etc. Para eso pueden usar eventos, flags, o simplemente revisar el estado de las cosas antes de entrar al loop principal de la tarea, y si no están listas, hacer un vTaskDelay y seguir revisando hasta que estén listas, para evitar que la tarea intente hacer cosas que no van a funcionar porque el sistema no está listo aún.
    dispatcher.registerCommand(std::make_unique<PinCommand>());
    dispatcher.registerCommand(std::make_unique<LedOnCommand>());
    dispatcher.registerCommand(std::make_unique<LedSetCommand>());
    dispatcher.registerCommand(std::make_unique<TempScanCommand>());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        seconds++;

        if (seconds % 600 == 0) {
            ESP_LOGI(TAG, "Proyecto: %u segundos activos", seconds);
        }

        // Aquí ponés tus tareas del loop del proyecto:
        // leer sensores, controlar actuadores, etc.
    }
}

void start_project_tasks()
{
    xTaskCreate(
        project_tasks,
        "project_tasks",
        4096,          // si usás std::string, subilo a 6144 o 8192
        NULL,
        5,
        NULL
    );

    //Tarea exclusiva one_wire
    LOGI(TAG,"Creando tareas y cola OneWire");
    q_onewire = xQueueCreate(10, sizeof(OneWireCommand));
    xTaskCreatePinnedToCore(task_onewire, "task_onewire", 4096, NULL, 5, NULL, 1);
    
    OneWireCommand init;
    LOGI(TAG,"Inicializando OneWire");
    init.type = OneWireCmdType::RESCAN;
    xQueueSend(q_onewire, &init, 0);
}
