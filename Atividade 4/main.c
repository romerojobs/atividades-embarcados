#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// ======================== CONFIG ========================

#define LED0 GPIO_NUM_4  // bit 0 (LSB)
#define LED1 GPIO_NUM_5
#define LED2 GPIO_NUM_6
#define LED3 GPIO_NUM_7  // bit 3 (MSB)

#define BTN_A GPIO_NUM_1
#define BTN_B GPIO_NUM_2

#define SAMPLE_PERIOD_MS 5
#define DEBOUNCE_TIME_MS 50
#define DEBOUNCE_COUNT (DEBOUNCE_TIME_MS / SAMPLE_PERIOD_MS)

// ======================== VARIÁVEIS ========================

static uint8_t counter = 0;
static uint8_t step = 1;

// Estrutura de debounce
typedef struct {
    uint8_t stable_state;
    uint8_t last_read;
    uint8_t count;
} button_t;

static button_t btnA = {1, 1, 0};
static button_t btnB = {1, 1, 0};

// ======================== FUNÇÕES ========================

// Atualiza LEDs conforme contador
void update_leds(uint8_t value) {
    gpio_set_level(LED0, (value >> 0) & 1);
    gpio_set_level(LED1, (value >> 1) & 1);
    gpio_set_level(LED2, (value >> 2) & 1);
    gpio_set_level(LED3, (value >> 3) & 1);
}

// Incrementa contador com overflow correto
void increment_counter() {
    counter = (counter + step) & 0x0F;
    update_leds(counter);
}

// Alterna passo entre 1 e 2
void toggle_step() {
    step = (step == 1) ? 2 : 1;
}

// Debounce + detecção de borda de descida
int process_button(button_t *btn, int gpio_level) {
    if (gpio_level == btn->last_read) {
        if (btn->count < DEBOUNCE_COUNT) {
            btn->count++;
        } else {
            // Estado estabilizado
            if (btn->stable_state != gpio_level) {
                btn->stable_state = gpio_level;

                // Detecta borda de descida (1 -> 0)
                if (gpio_level == 0) {
                    return 1; // evento de clique
                }
            }
        }
    } else {
        btn->count = 0;
    }

    btn->last_read = gpio_level;
    return 0;
}

// Callback do timer
void timer_callback(void *arg) {
    int levelA = gpio_get_level(BTN_A);
    int levelB = gpio_get_level(BTN_B);

    if (process_button(&btnA, levelA)) {
        increment_counter();
    }

    if (process_button(&btnB, levelB)) {
        toggle_step();
    }
}

// ======================== SETUP ========================

void setup_gpio() {
    // LEDs como saída
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED0) | (1ULL << LED1) |
                        (1ULL << LED2) | (1ULL << LED3),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    // Botões como entrada com pull-up
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BTN_A) | (1ULL << BTN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0
    };
    gpio_config(&btn_conf);
}

// ======================== MAIN ========================

void app_main(void) {
    setup_gpio();
    update_leds(counter);

    // Cria timer periódico
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "button_timer"
    };

    esp_timer_handle_t timer;
    esp_timer_create(&timer_args, &timer);

    // Inicia timer (em microssegundos)
    esp_timer_start_periodic(timer, SAMPLE_PERIOD_MS * 1000);
}