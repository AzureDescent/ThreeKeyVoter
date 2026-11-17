#include <stdio.h>
#include <string.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// 网关MAC
static uint8_t gw_mac[] = {0xF8,0x5B,0x1B,0x9A,0x61,0xF0};

#define BTN1_GPIO      GPIO_NUM_4
#define BTN2_GPIO      GPIO_NUM_5
#define BTN3_GPIO      GPIO_NUM_6
#define DEBOUNCE_MS    30
#define HEARTBEAT_MS   3000
#define VAL_BTN1       1
#define VAL_BTN2       2
#define VAL_BTN3       3
#define VAL_HEARTBEAT  0

static esp_err_t send_data(uint8_t val)
{
    return esp_now_send(gw_mac, &val, 1);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // GPIO初始化 内部上拉
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << BTN1_GPIO) | (1ULL << BTN2_GPIO) | (1ULL << BTN3_GPIO);
    gpio_config(&io_conf);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, gw_mac, 6);
    peer_info.encrypt = false;
    peer_info.channel = 0;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    uint8_t tx_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, tx_mac);
    printf("TX BOARD MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           tx_mac[0],tx_mac[1],tx_mac[2],tx_mac[3],tx_mac[4],tx_mac[5]);

    TickType_t last_hb = xTaskGetTickCount();
    bool last1 = gpio_get_level(BTN1_GPIO);
    bool last2 = gpio_get_level(BTN2_GPIO);
    bool last3 = gpio_get_level(BTN3_GPIO);

    while(1)
    {
        // 按键1
        bool now1 = gpio_get_level(BTN1_GPIO);
        if(now1 != last1)
        {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            now1 = gpio_get_level(BTN1_GPIO);
            if(now1 != last1)
            {
                if(last1 == 1 && now1 == 0)
                {
                    printf("KEY1 pressed -> send 1\n");
                    send_data(VAL_BTN1);
                }
                last1 = now1;
            }
        }
        // 按键2
        bool now2 = gpio_get_level(BTN2_GPIO);
        if(now2 != last2)
        {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            now2 = gpio_get_level(BTN2_GPIO);
            if(now2 != last2)
            {
                if(last2 == 1 && now2 == 0)
                {
                    printf("KEY2 pressed -> send 2\n");
                    send_data(VAL_BTN2);
                }
                last2 = now2;
            }
        }
        // 按键3
        bool now3 = gpio_get_level(BTN3_GPIO);
        if(now3 != last3)
        {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            now3 = gpio_get_level(BTN3_GPIO);
            if(now3 != last3)
            {
                if(last3 == 1 && now3 == 0)
                {
                    printf("KEY3 pressed -> send 3\n");
                    send_data(VAL_BTN3);
                }
                last3 = now3;
            }
        }

        // 心跳包
        TickType_t t = xTaskGetTickCount();
        if((t - last_hb) >= pdMS_TO_TICKS(HEARTBEAT_MS))
        {
            last_hb = t;
            printf("send heartbeat 0\n");
            send_data(VAL_HEARTBEAT);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
