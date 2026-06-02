class LedOnCommand : public Command {
public:
    const char* name() override { return "ledon"; }
    int minArgs() override { return 1; } // Requiere el número de pin

    int execute(uint32_t src, const std::vector<std::string>& args) override {
        int pin = std::stoi(args[0]);

        // Configurar el GPIO (Lógica específica de hardware)
        gpio_reset_pin((gpio_num_t)pin);
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pin, 1);

        ESP_LOGI("CMD", "LED en pin %d encendido", pin);
        return 0;
    }
};