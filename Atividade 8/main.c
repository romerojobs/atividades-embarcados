#include <stdio.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_oneshot.h" 
#include "driver/ledc.h"
#include "driver/gpio.h"

#define LED_GPIO 5  // Altere se necessário
#define BUTTON_GPIO 6

adc_oneshot_unit_handle_t adc_handle;

void button_init()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // botão normalmente aberto
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

void adc_init() { 
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1, }; 
    adc_oneshot_new_unit(&init_config, &adc_handle); 
    adc_oneshot_chan_cfg_t config = { 
        .bitwidth = 12, 
        .atten = ADC_ATTEN_DB_12
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_3, &config);
}

void ler_adc(int *variavel){ 
    adc_oneshot_read(adc_handle, ADC_CHANNEL_3, variavel); 
}

void led_init()
{ 
    ledc_timer_config_t timer = { 
        .speed_mode = LEDC_LOW_SPEED_MODE, 
        .timer_num = LEDC_TIMER_0, 
        .duty_resolution = LEDC_TIMER_12_BIT, 
        .freq_hz = 1000, 
        .clk_cfg = LEDC_AUTO_CLK 
    }; ledc_timer_config(&timer);

    ledc_channel_config_t channel = { 
    .gpio_num = LED_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE, 
    .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, 
    .duty = 0, 
    .hpoint = 0 }; 

ledc_channel_config(&channel); 
}
void alterar_duty_pwm_led(int valor)
{ 
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, valor); 
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0); 
}

void app_main() {
    adc_init(); 
    led_init();
    button_init();

    bool congelado = false;
    int valor_congelado = 0;
    int estado_anterior = 1;

    while(true)
    { 
        int valor_adc_atual;
        int estado_botao = gpio_get_level(BUTTON_GPIO);

        // Detecta borda de descida (botão pressionado)
        if (estado_anterior == 1 && estado_botao == 0) {
            congelado = !congelado; // alterna estado

            if (congelado) {
                ler_adc(&valor_congelado); // salva valor atual
            }
        }

        estado_anterior = estado_botao;

        if (!congelado) {
            ler_adc(&valor_adc_atual);
            alterar_duty_pwm_led(valor_adc_atual);
            printf("ADC: %d\n", valor_adc_atual);
        } else {
            alterar_duty_pwm_led(valor_congelado);
            printf("CONGELADO: %d\n", valor_congelado);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
