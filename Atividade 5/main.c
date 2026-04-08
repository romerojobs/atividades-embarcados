#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Definições de pinos
#define LED_PIN     4
#define BUTTON_PIN  2

// Tempo de debounce (ms)
#define DEBOUNCE_TIME 50

// Tempo de desligamento automático (us)
#define AUTO_OFF_TIME 10000000  // 10 segundos

// Estado do LED
static bool led_state = false;

// Timer
esp_timer_handle_t auto_off_timer;

// Callback do timer (executa fora do loop principal)
void auto_off_callback(void* arg) {
    gpio_set_level(LED_PIN, 0);
    led_state = false;
    printf("LED desligado automaticamente\n");
}

// Inicialização do timer
void init_timer() {
    const esp_timer_create_args_t timer_args = {
        .callback = &auto_off_callback,
        .name = "auto_off_timer"
    };

    esp_timer_create(&timer_args, &auto_off_timer);
}

// Configuração dos GPIOs
void init_gpio() {
    gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&led_config);

    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&button_config);
}

void app_main() {
    init_gpio();
    init_timer();

    int last_button_state = 1;
    int stable_state = 1;
    int last_reading = 1;

    int64_t last_debounce_time = 0;

    while (1) {
        int reading = gpio_get_level(BUTTON_PIN);

        // Detecta mudança (possível bounce)
        if (reading != last_reading) {
            last_debounce_time = esp_timer_get_time();
        }

        // Verifica se passou tempo de debounce
        if ((esp_timer_get_time() - last_debounce_time) > (DEBOUNCE_TIME * 1000)) {

            // Estado estabilizado mudou
            if (reading != stable_state) {
                stable_state = reading;

                // Detecta pressionamento (nível LOW)
                if (stable_state == 0) {

                    // Toggle do LED
                    led_state = !led_state;
                    gpio_set_level(LED_PIN, led_state);

                    if (led_state) {
                        printf("LED ligado\n");

                        // Reinicia timer de 10s
                        esp_timer_stop(auto_off_timer); // garante reset
                        esp_timer_start_once(auto_off_timer, AUTO_OFF_TIME);
                    } else {
                        printf("LED desligado manualmente\n");

                        // Cancela timer
                        esp_timer_stop(auto_off_timer);
                    }
                }
            }
        }

        last_reading = reading;

        // Polling interval
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}