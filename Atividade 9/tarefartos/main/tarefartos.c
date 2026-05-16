#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_adc/adc_oneshot.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_err.h"

/*
========================================================
GPIOs ESP32-S3
========================================================
*/

#define GPIO_BUTTON               4
#define PWM_GPIO                  5

#define I2C_MASTER_SDA_IO         8
#define I2C_MASTER_SCL_IO         9

/*
========================================================
ADC
========================================================
*/

#define ADC_POT_CHANNEL           ADC_CHANNEL_6
#define ADC_POT_UNIT              ADC_UNIT_1

/*
========================================================
I2C
========================================================
*/

#define I2C_MASTER_FREQ_HZ        100000
#define I2C_MASTER_PORT           I2C_NUM_0

/*
========================================================
MPU6050
========================================================
*/

#define MPU6050_ADDR              0x68

#define MPU6050_PWR_MGMT_1        0x6B

#define MPU6050_ACCEL_XOUT_H      0x3B
#define MPU6050_ACCEL_YOUT_H      0x3D
#define MPU6050_ACCEL_ZOUT_H      0x3F

#define MPU6050_ACCEL_SCALE       16384.0f

/*
========================================================
PWM
========================================================
*/

#define PWM_FREQUENCY             50
#define PWM_RESOLUTION            LEDC_TIMER_13_BIT

#define PWM_MODE                  LEDC_LOW_SPEED_MODE
#define PWM_TIMER                 LEDC_TIMER_0
#define PWM_CHANNEL               LEDC_CHANNEL_0

#define PWM_MAX_DUTY              ((1 << 13) - 1)

/*
========================================================
ESTADO
========================================================
*/

#define PWM_LIVE                  0
#define PWM_HOLD                  1

int estado_PWM = PWM_LIVE;

/*
========================================================
HANDLES RTOS
========================================================
*/

QueueHandle_t xQueuePotenciometro;

SemaphoreHandle_t xMutexDados;
SemaphoreHandle_t xSemaforoPWM;
SemaphoreHandle_t xSemaforoBotao;

/*
========================================================
HANDLES DRIVERS
========================================================
*/

adc_oneshot_unit_handle_t adc_handle;

i2c_master_bus_handle_t i2c_bus_handle;
i2c_master_dev_handle_t mpu6050_handle;

/*
========================================================
STRUCTS
========================================================
*/

typedef struct
{
    char status[6];

    int valor_potenciometro;
    int tensao_potenciometro;
    int porcentagem_led;

    float x_sensor;
    float y_sensor;
    float z_sensor;

} ConsoleData;


typedef struct
{
    float x;
    float y;
    float z;

} leitura_mpu_t;

/*
========================================================
DADOS GLOBAIS
========================================================
*/

ConsoleData dados_atuais;

/*
========================================================
PWM INIT
========================================================
*/

void pwm_init()
{
    ledc_timer_config_t timer_config = {
        .speed_mode       = PWM_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_RESOLUTION,
        .freq_hz          = PWM_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };

    ledc_timer_config(&timer_config);

    ledc_channel_config_t channel_config = {
        .gpio_num   = PWM_GPIO,
        .speed_mode = PWM_MODE,
        .channel    = PWM_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = PWM_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };

    ledc_channel_config(&channel_config);
}

/*
========================================================
ADC INIT
========================================================
*/

void adc_init()
{
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = ADC_POT_UNIT,
    };

    adc_oneshot_new_unit(
        &adc_config,
        &adc_handle
    );

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };

    adc_oneshot_config_channel(
        adc_handle,
        ADC_POT_CHANNEL,
        &channel_config
    );
}

/*
========================================================
I2C INIT
========================================================
*/

void i2c_init()
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_PORT,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    i2c_new_master_bus(
        &bus_config,
        &i2c_bus_handle
    );
}

/*
========================================================
MPU INIT
========================================================
*/

void mpu6050_init()
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    i2c_master_bus_add_device(
        i2c_bus_handle,
        &dev_config,
        &mpu6050_handle
    );

    uint8_t wakeup_cmd[2] = {
        MPU6050_PWR_MGMT_1,
        0x00
    };

    i2c_master_transmit(
        mpu6050_handle,
        wakeup_cmd,
        sizeof(wakeup_cmd),
        -1
    );
}

/*
========================================================
LEITURA MPU
========================================================
*/

int16_t mpu6050_read_axis(uint8_t reg)
{
    uint8_t data[2];

    i2c_master_transmit_receive(
        mpu6050_handle,
        &reg,
        1,
        data,
        2,
        -1
    );

    return (int16_t)((data[0] << 8) | data[1]);
}

/*
========================================================
ADC READ
========================================================
*/

int lerADC()
{
    int leitura = 0;

    adc_oneshot_read(
        adc_handle,
        ADC_POT_CHANNEL,
        &leitura
    );

    return leitura;
}

/*
========================================================
MPU READ
========================================================
*/

leitura_mpu_t lerMPU()
{
    leitura_mpu_t leitura;

    int16_t raw_x = mpu6050_read_axis(MPU6050_ACCEL_XOUT_H);
    int16_t raw_y = mpu6050_read_axis(MPU6050_ACCEL_YOUT_H);
    int16_t raw_z = mpu6050_read_axis(MPU6050_ACCEL_ZOUT_H);

    leitura.x = raw_x / MPU6050_ACCEL_SCALE;
    leitura.y = raw_y / MPU6050_ACCEL_SCALE;
    leitura.z = raw_z / MPU6050_ACCEL_SCALE;

    return leitura;
}

/*
========================================================
ALTERAR ESTADO
========================================================
*/

void alterarEstado()
{
    xSemaphoreTake(xMutexDados, portMAX_DELAY);

    estado_PWM = !estado_PWM;

    if(estado_PWM == PWM_HOLD)
    {
        strcpy(dados_atuais.status, "HOLD");
    }
    else
    {
        strcpy(dados_atuais.status, "LIVE");
    }

    xSemaphoreGive(xMutexDados);
}

/*
========================================================
ENVIAR PWM
========================================================
*/

void enviarPWM(int *entrada)
{
    uint32_t duty = (*entrada * PWM_MAX_DUTY) / 4095;

    ledc_set_duty(
        PWM_MODE,
        PWM_CHANNEL,
        duty
    );

    ledc_update_duty(
        PWM_MODE,
        PWM_CHANNEL
    );
}

/*
========================================================
ISR BOTÃO
========================================================
*/

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xSemaphoreGiveFromISR(
        xSemaforoBotao,
        &xHigherPriorityTaskWoken
    );

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*
========================================================
BUTTON INIT
========================================================
*/

void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&io_conf);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(
        GPIO_BUTTON,
        gpio_isr_handler,
        NULL
    );
}

/*
========================================================
TASK BUTTON
========================================================
*/

void TaskButton(void * pvParameters)
{
    TickType_t ultimo_clique = 0;

    const TickType_t debounce_delay =
        pdMS_TO_TICKS(80);

    while(1)
    {
        if(xSemaphoreTake(
            xSemaforoBotao,
            portMAX_DELAY))
        {
            TickType_t agora = xTaskGetTickCount();

            if((agora - ultimo_clique)
                > debounce_delay)
            {
                ultimo_clique = agora;

                xSemaphoreGive(xSemaforoPWM);
            }
        }
    }
}

/*
========================================================
TASK PWM
========================================================
*/

void TaskPWM(void * pvParameters)
{
    int ultimo_pwm = 0;

    while(1)
    {
        if(xSemaphoreTake(
            xSemaforoPWM,
            0) == pdTRUE)
        {
            alterarEstado();
        }

        int leitura;

        if(xQueueReceive(
            xQueuePotenciometro,
            &leitura,
            0) == pdTRUE)
        {
            ultimo_pwm = leitura;
        }

        enviarPWM(&ultimo_pwm);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
/*
========================================================
TASK CONSOLE
========================================================
*/

void TaskConsole(void * pvParameters)
{
    while(1)
    {
        xSemaphoreTake(xMutexDados, portMAX_DELAY);

        ConsoleData dados_locais = dados_atuais;

        xSemaphoreGive(xMutexDados);

        printf("=====================================================\n");

        printf(
            "STATUS: [%s] | POT: %d (%d mV) | LED: %d%%\n",
            dados_locais.status,
            dados_locais.valor_potenciometro,
            dados_locais.tensao_potenciometro,
            dados_locais.porcentagem_led
        );

        printf(
            "IMU ACCEL (g): X: %.2f | Y: %.2f | Z: %.2f\n",
            dados_locais.x_sensor,
            dados_locais.y_sensor,
            dados_locais.z_sensor
        );

        printf("=====================================================\n");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*
========================================================
TASK POTENCIÔMETRO
========================================================
*/

void TaskPotenciometro(void * pvParameters)
{
    while(1)
    {
        /*
        ========================================================
        SOMENTE atualiza ADC no modo LIVE
        ========================================================
        */

        if(estado_PWM == PWM_LIVE)
        {
            int leitura = lerADC();

            xQueueOverwrite(
                xQueuePotenciometro,
                &leitura
            );

            int tensao =
                (leitura * 3300) / 4095;

            int porcentagem =
                (leitura * 100) / 4095;

            xSemaphoreTake(
                xMutexDados,
                portMAX_DELAY
            );

            dados_atuais.valor_potenciometro =
                leitura;

            dados_atuais.tensao_potenciometro =
                tensao;

            dados_atuais.porcentagem_led =
                porcentagem;

            xSemaphoreGive(xMutexDados);
        }

        /*
        ========================================================
        HOLD
        ========================================================
        */

        else
        {
            /*
            Não faz nada.
            Mantém os últimos dados.
            */
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/*
========================================================
TASK SENSOR
========================================================
*/

void TaskSensor(void * pvParameters)
{
    while(1)
    {
        leitura_mpu_t dados = lerMPU();

        xSemaphoreTake(xMutexDados, portMAX_DELAY);

        dados_atuais.x_sensor = dados.x;
        dados_atuais.y_sensor = dados.y;
        dados_atuais.z_sensor = dados.z;

        xSemaphoreGive(xMutexDados);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*
========================================================
APP MAIN
========================================================
*/

void app_main()
{
    strcpy(dados_atuais.status, "LIVE");

    pwm_init();
    button_init();
    adc_init();
    i2c_init();
    mpu6050_init();

    xQueuePotenciometro = xQueueCreate(
        1,
        sizeof(int)
    );

    xMutexDados = xSemaphoreCreateMutex();

    xSemaforoPWM = xSemaphoreCreateBinary();
    xSemaforoBotao = xSemaphoreCreateBinary();

    xTaskCreate(
        TaskPWM,
        "TaskPWM",
        2048,
        NULL,
        3,
        NULL
    );

    xTaskCreate(
        TaskConsole,
        "TaskConsole",
        4096,
        NULL,
        1,
        NULL
    );

    xTaskCreate(
        TaskSensor,
        "TaskSensor",
        2048,
        NULL,
        2,
        NULL
    );

    xTaskCreate(
        TaskPotenciometro,
        "TaskPotenciometro",
        2048,
        NULL,
        2,
        NULL
    );

    xTaskCreate(
        TaskButton,
        "TaskButton",
        2048,
        NULL,
        4,
        NULL
    );
}
