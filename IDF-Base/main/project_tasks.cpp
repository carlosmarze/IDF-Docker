#include "sched_tasks.h"
#include "project_tasks.h"
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
    const char* usage() const override { return "<pin>"; }
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


static void project_tasks(void* arg)
{
    LOGI(TAG, "Tareas del proyecto iniciadas"); 
    uint32_t seconds = 0;
    //verificar acá lo que necesite la tarea, por ejemplo si necesita esperar a que el WiFi esté conectado, o a que el NTP esté sincronizado, o a que algún recurso esté listo, etc. Para eso pueden usar eventos, flags, o simplemente revisar el estado de las cosas antes de entrar al loop principal de la tarea, y si no están listas, hacer un vTaskDelay y seguir revisando hasta que estén listas, para evitar que la tarea intente hacer cosas que no van a funcionar porque el sistema no está listo aún.
    dispatcher.registerCommand(std::make_unique<PinCommand>());
    dispatcher.registerCommand(std::make_unique<LedOnCommand>());
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
}
