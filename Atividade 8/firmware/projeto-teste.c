#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

// Definições de Pinos
#define LED_GPIO       5
#define BUTTON_GPIO    14
#define ADC_CHAN       ADC_CHANNEL_3 // GPIO 4 no ESP32 (verifique o datasheet do seu modelo)

// Variáveis globais
volatile bool congelado = false;
volatile int valor_adc_raw = 0;
volatile int voltagem_mv = 0;
volatile int voltagem_congelada_mv = 0;
volatile int raw_congelado = 0;

adc_oneshot_unit_handle_t adc_handle;

// Inicialização do Botão
void button_init() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

// Inicialização do ADC
void adc_init() {
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12 // Permite leitura até ~3.3V
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHAN, &config);
}

// Inicialização do PWM para o LED
void led_init() {
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel);
}

void alterar_duty_pwm_led(int valor) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, valor);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// TASK - Processamento de ADC e PWM
void task_adc_pwm(void *pvParameters) {
    int leitura_atual = 0;
    while (true) {
        if (!congelado) {
            // Realiza a leitura bruta (0-4095)
            adc_oneshot_read(adc_handle, ADC_CHAN, &leitura_atual);
            valor_adc_raw = leitura_atual;
            
            // Converte para milivolts: (Leitura * 3300mV) / 4095
            voltagem_mv = (leitura_atual * 3300) / 4095;
            
            // Guarda para o caso de congelar
            raw_congelado = valor_adc_raw;
            voltagem_congelada_mv = voltagem_mv;

            alterar_duty_pwm_led(valor_adc_raw);
        } else {
            // Se congelado, mantém o LED no valor travado
            alterar_duty_pwm_led(raw_congelado);
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// TASK - Controle do Botão (Toggle)
void task_botao(void *pvParameters) {
    int estado_anterior = 1;
    while (true) {
        int estado_atual = gpio_get_level(BUTTON_GPIO);

        // Detecta borda de descida (botão pressionado)
        if (estado_anterior == 1 && estado_atual == 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (gpio_get_level(BUTTON_GPIO) == 0) {
                congelado = !congelado;
                printf("\n>>> ESTADO ALTERADO: %s <<<\n", congelado ? "CONGELADO" : "LIVRE");

                // Espera soltar o botão para não inverter de novo no próximo ciclo
                while (gpio_get_level(BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        estado_anterior = estado_atual;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// TASK - Monitoramento Serial
void task_print(void *pvParameters) {
    while (true) {
        if (!congelado) {
            printf("Monitor: %d mV | Raw: %d\n", voltagem_mv, valor_adc_raw);
        } else {
            printf("Monitor [CONGELADO]: %d mV\n", voltagem_congelada_mv);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main() {
    // Inicializações
    adc_init();
    led_init();
    button_init();

    // Criação das Tasks (FreeRTOS)
    xTaskCreate(task_adc_pwm, "task_adc_pwm", 2048, NULL, 2, NULL);
    xTaskCreate(task_botao,   "task_botao",   2048, NULL, 1, NULL);
    xTaskCreate(task_print,   "task_print",   4096, NULL, 1, NULL);
}