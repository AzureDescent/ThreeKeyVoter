#include <stdio.h>
#include <string.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 发射端MAC
static uint8_t src_mac[] = {0xF8,0x5B,0x1B,0x9B,0xC9,0xE8};

static void espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    // MAC过滤，只处理指定发射板
    if(memcmp(esp_now_info->src_addr, src_mac, 6) != 0)
    {
        return;
    }
    if(data_len < 1) return;
    uint8_t val = data[0];
    if(val == 0)
    {
        printf("[Heartbeat] Link alive\n");
    }
    else if(val == 1)
    {
        printf("[Event] KEY1\n");
    }
    else if(val == 2)
    {
        printf("[Event] KEY2\n");
    }
    else if(val == 3)
    {
        printf("[Event] KEY3\n");
    }
    else
    {
        printf("[Unknown] recv = %d\n", val);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    printf("Gateway ready, waiting ESP-NOW data...\n");

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
