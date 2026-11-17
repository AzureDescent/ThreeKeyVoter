/*
 * student.c 发射端 v1 协议，适配 ESP-IDF 6.1
 * 信道：1，协议版本：1
 * 修复：v6.1 新版send回调原型，不能传NULL
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

static const char *TAG = "student";

// ===================== 配置区（这里改！）=====================
#define ESPNOW_CHANNEL      1
#define PROTOCOL_VERSION    1

#define MY_NODE_ID          1       // student1=1，student2=2，student3=3
#define HEARTBEAT_PERIOD_MS 2000
#define KEY_SEND_RETRY_CNT  2
#define RETRY_INTERVAL_MS   50

#define GPIO_KEY1           GPIO_NUM_4
#define GPIO_KEY2           GPIO_NUM_5
#define GPIO_KEY3           GPIO_NUM_6

// 网关MAC，改成网关串口打印的真实MAC
static const uint8_t gw_mac[6] = {0xF8,0x5B,0x1B,0x9A,0x61,0xF0};
// ===========================================================

#define VOTE_UNDERSTAND         0
#define VOTE_UNSURE             1
#define VOTE_NOT_UNDERSTAND     2
#define VOTE_HEARTBEAT          3

typedef struct {
    uint8_t  ver;
    uint8_t  node_id;
    uint8_t  vote;
    uint32_t sequence;
} __attribute__((packed)) vote_packet_t;

static uint32_t seq_cnt = 0;
static esp_now_peer_info_t peer_info = {0};

// ========== IDF6.1 新版发送回调！！原型不能写错 ==========
static void esp_now_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    (void)status;
}

static esp_err_t send_vote_pkt(uint8_t vote_type)
{
    vote_packet_t pkt;
    pkt.ver = PROTOCOL_VERSION;
    pkt.node_id = MY_NODE_ID;
    pkt.vote = vote_type;
    pkt.sequence = seq_cnt++;

    esp_err_t ret = esp_now_send(gw_mac, (uint8_t *)&pkt, sizeof(vote_packet_t));
    ESP_LOGI(TAG, "send vote=%u, seq=%lu, ret=%s", vote_type, (unsigned long)pkt.sequence, esp_err_to_name(ret));
    return ret;
}

static void key_init(void)
{
    gpio_config_t io_conf = {0};
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pin_bit_mask = (1ULL << GPIO_KEY1) | (1ULL << GPIO_KEY2) | (1ULL << GPIO_KEY3);
    gpio_config(&io_conf);
}

static bool read_key(gpio_num_t gpio)
{
    return gpio_get_level(gpio) == 0;
}

static void heartbeat_task(void *arg)
{
    for (;;)
    {
        send_vote_pkt(VOTE_HEARTBEAT);
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ThreeKeyVoter Student Node ID=%u", MY_NODE_ID);

    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(esp_now_init());
    // 注册【新版格式回调】，不再传NULL！
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));

    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, gw_mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));

    key_init();
    xTaskCreate(heartbeat_task, "heartbeat", 2048, NULL, 5, NULL);

    bool key1_last = false;
    bool key2_last = false;
    bool key3_last = false;

    for (;;)
    {
        bool k1 = read_key(GPIO_KEY1);
        bool k2 = read_key(GPIO_KEY2);
        bool k3 = read_key(GPIO_KEY3);

        if (k1 && !key1_last)
        {
            ESP_LOGI(TAG, "KEY1 pressed: UNDERSTAND");
            send_vote_pkt(VOTE_UNDERSTAND);
            
        }
        if (k2 && !key2_last)
        {
            ESP_LOGI(TAG, "KEY2 pressed: UNSURE");
            send_vote_pkt(VOTE_UNSURE);
            
        }
        if (k3 && !key3_last)
        {
            ESP_LOGI(TAG, "KEY3 pressed: NOT_UNDERSTAND");
            send_vote_pkt(VOTE_NOT_UNDERSTAND);
            
        }

        key1_last = k1;
        key2_last = k2;
        key3_last = k3;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
