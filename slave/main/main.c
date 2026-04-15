#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "hal/gpio_types.h"
#include "hal/ledc_types.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "portmacro.h"
#include "esp_err.h"
#include "iot_servo.h"
#include "pthread.h"
#include "esp_timer.h"
#include "main.h"

#define QUEUE_SIZE 10
#define PAYLOAD_SIZE sizeof(Payload)
#define SERVO_WAIT_TIME 100
#define MAX_DEGREES 180
#define ANGLE_INCREMENT 36
#define NUM_CHUTES 4
#define INDEXER_DEG_START 32
Payload payload;
static StaticQueue_t recv_queue;
static QueueHandle_t recv_handler;
static uint8_t recv_data[QUEUE_SIZE * PAYLOAD_SIZE]__attribute__((aligned(4)));

const int DC_AIN1 = 4;
const int DC_AIN2 = 14;
const int DC_MOTOR_A_PWMA_PIN = 13;
const int DC_BIN1 = 5;
const int DC_BIN2 = 18;
const int DC_MOTOR_B_PWMA_PIN = 17;
const int STBY_PIN = 27;
const int PITCH_PIN = 19;
const int YAW_PIN = 21; 
const int INDEXER_PIN = 18; 

typedef struct {
    ledc_channel_t channel;
    uint8_t current_angle;
    uint64_t last_move_time; 
    uint64_t wait_time; 
} Servo;

Servo PitchServo = {.channel = LEDC_CHANNEL_0, .current_angle = 0, .last_move_time = 0, .wait_time = SERVO_WAIT_TIME};
Servo YawServo = {.channel = LEDC_CHANNEL_1, .current_angle = 0, .last_move_time = 0, .wait_time = SERVO_WAIT_TIME};
Servo IndexerServo = {.channel = LEDC_CHANNEL_2, .current_angle = INDEXER_DEG_START, .last_move_time = 0, .wait_time = SERVO_WAIT_TIME};

typedef enum {
    SERVO_LEFT,
    SERVO_RIGHT,
    SERVO_UP,
    SERVO_DOWN
} ServoDirection;

void initializeNVS() 
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void initializeWifi()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void data_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) 
{
    // ESP_LOGI("SLAVE", "Message received from Master!");

    if (xQueueSend(recv_handler, data, 0) != pdPASS) {
        ESP_LOGI("RECV DATA", "FAILED TO POST DATA TO CONSUMER");
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void move_servo(Servo *servo, ServoDirection direction)
{
    // get the current time
    uint64_t current_time = esp_timer_get_time();

    if ((current_time - servo->last_move_time) < servo->wait_time) {
        return;
    }

    switch (direction) {
        case SERVO_UP:
        case SERVO_RIGHT:
        if (servo->current_angle >= MAX_DEGREES) {
            return;
        }
        servo->current_angle += 3;
        break;
        case SERVO_DOWN:
        case SERVO_LEFT:
        if (servo->current_angle <= 0) {
            return;
        }
        servo->current_angle -= 3;
        break;
    }
    iot_servo_write_angle(LEDC_HIGH_SPEED_MODE, servo->channel, servo->current_angle);
    servo->last_move_time = current_time;
}

void move_indexer(Servo *indexer, uint8_t *indexer_position)
{
    ESP_LOGI("MOVE INDEXER", "MOVE INDEXER METHOD CALLED");
    (*indexer_position)++;
    if (*indexer_position > NUM_CHUTES) {
        // reset 
        *indexer_position = 0;
        indexer->current_angle = INDEXER_DEG_START;
        iot_servo_write_angle(LEDC_HIGH_SPEED_MODE,IndexerServo.channel, indexer->current_angle);
        return;
    }

    if (IndexerServo.current_angle <= 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    } else if (IndexerServo.current_angle >= 180) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }

    IndexerServo.current_angle += ANGLE_INCREMENT;
    iot_servo_write_angle(LEDC_HIGH_SPEED_MODE, IndexerServo.channel, IndexerServo.current_angle);
}

void handle_recv_data_task(void* params)
{
    // this task must never return or end!   
    Payload recv_payload;
    bool last_indexer_state = false;
    uint8_t indexer_position = 0;
    while (true) {
        xQueueReceive(recv_handler, &recv_payload, portMAX_DELAY);

        // ESP_LOGI("COMMANDS", "UP: %d, DOWN: %d, LEFT: %d, RIGHT: %d, INDEXER: %d", 
        //                                                         recv_payload.upState, 
        //                                                         recv_payload.downState, 
        //                                                         recv_payload.leftState, 
        //                                                         recv_payload.rightState, 
        //                                                         recv_payload.indexerState);

        if (recv_payload.upState) {
            move_servo(&PitchServo, SERVO_UP);
        } else if (recv_payload.downState) {
            move_servo(&PitchServo,SERVO_DOWN);
        } else if (recv_payload.leftState) {
            move_servo(&YawServo, SERVO_LEFT);
        } else if(recv_payload.rightState) {
            move_servo(&YawServo, SERVO_RIGHT);
        } else if(recv_payload.indexerState && last_indexer_state == false) {
            move_indexer(&IndexerServo, &indexer_position);
        }
        last_indexer_state = recv_payload.indexerState;
    }
}
void app_main(void)
{
    initializeNVS();
    initializeWifi();
    recv_handler = xQueueCreateStatic(QUEUE_SIZE, 
                                    PAYLOAD_SIZE,
                                    recv_data,
                                    &recv_queue);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(data_recv_cb));
    // esp_log_level_set("ESPNOW", ESP_LOG_WARN);      
    // configure all servos at once
    servo_config_t servo_cfg = {
        .max_angle = 180,
        .min_width_us = 500,
        .max_width_us = 2500,
        .freq = 50,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {
                PITCH_PIN,
                YAW_PIN,
                INDEXER_PIN,
            },
            .ch = {
                LEDC_CHANNEL_0,
                LEDC_CHANNEL_1,
                LEDC_CHANNEL_2
            },
        },
        .channel_number = 3,
    };
    // set initial angles 
    iot_servo_init(LEDC_HIGH_SPEED_MODE, &servo_cfg);
    vTaskDelay(pdMS_TO_TICKS(500));
    iot_servo_write_angle(LEDC_HIGH_SPEED_MODE, PitchServo.channel, PitchServo.current_angle);
    iot_servo_write_angle(LEDC_HIGH_SPEED_MODE, YawServo.channel, YawServo.current_angle);
    iot_servo_write_angle(LEDC_HIGH_SPEED_MODE, IndexerServo.channel, IndexerServo.current_angle);


    // setup dc motors 
    ESP_LOGI("MOTOR", "Starting motor init");
    ledc_timer_config_t dc_motor_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 5000,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&dc_motor_config));

    ledc_channel_config_t dc_motor_a = {
        .gpio_num = DC_MOTOR_A_PWMA_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_3,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0, // init the motor at 0 avg speed 
    };
    ESP_ERROR_CHECK(ledc_channel_config(&dc_motor_a));

    ledc_channel_config_t dc_motor_b = {
        .gpio_num = DC_MOTOR_B_PWMA_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_4,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0, // init the motor at 0 avg speed 
    };
    ESP_ERROR_CHECK(ledc_channel_config(&dc_motor_b));

    // setpins to output since default is input on GPIO pins 
    gpio_set_direction(DC_AIN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(DC_AIN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(STBY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DC_BIN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(DC_BIN2, GPIO_MODE_OUTPUT);

    // set clockwise/counterclockwise spin direction for dc motor 
    gpio_set_level(DC_AIN2, 0);
    gpio_set_level(DC_AIN1, 1);
    gpio_set_level(DC_BIN2, 0);
    gpio_set_level(DC_BIN1, 1);
    gpio_set_level(STBY_PIN, 1);
    // set and update duty cycle of motors 
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, dc_motor_a.channel, 512);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, dc_motor_a.channel);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, dc_motor_b.channel, 512);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, dc_motor_b.channel);

    xTaskCreatePinnedToCore(handle_recv_data_task, 
        "Receiving Data Task" ,
                            4096, 
                            NULL, 
                            4, 
                            NULL, 
                            1);
}
