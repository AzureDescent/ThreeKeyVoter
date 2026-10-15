/*
 * 学生端 · 三键投票器 —— 当前阶段：跳过按键，直接周期发送 Mock 投票
 * ============================================================================
 * 负责方向：学生端固件
 *
 * 最终目标（任务书第 2 节）：
 *   Button 1 -> UNDERSTAND
 *   Button 2 -> UNSURE
 *   Button 3 -> NOT_UNDERSTAND
 *   按下按钮后：对应 LED 反馈 + 串口打印状态
 *   重点是搞清楚：GPIO 输入 / GPIO 输出 / 按键读取 / 基本消抖 / 串口调试
 *
 * 现在做到的（还没接按键，先打通无线链路）：
 *   每 1 秒轮换发一条 Mock 投票给网关：
 *       学生端 --ESP-NOW--> 网关 --USB Serial--> PC
 *   LED 每发一次闪一下，用来肉眼确认板子在工作。
 *
 * 待实现（TODO）：
 *   [ ] GPIO 输入配置（按键；注意上拉/下拉，悬空会乱跳）
 *   [ ] 按键扫描 + 基本消抖，替换掉下面的 Mock 轮换
 *   [ ] Wi-Fi Station 模式连手机热点，打印 "Connected" 和 IP
 *   [ ] 把 vote 状态做成三态机
 *   [ ] 电量 / 独立供电
 *
 * 已实测的板级信息（详见仓库根目录 README「硬件」章节）：
 *   - 板载 LED：GPIO8，蓝灯，低电平点亮（gpio_set_level(8, 0) 是亮）
 *   - BOOT 键：GPIO9
 *   - 控制台：USB Serial/JTAG（USB-C 直连原生 USB，本机枚举为 COM38）
 *   - 按键接哪几个脚由你自己定，定下来后写进这里和 README
 * ============================================================================
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "driver/gpio.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"

static const char *TAG = "student";

/* ---------------------------------------------------------------------------
 * 关键配置
 * ------------------------------------------------------------------------ */

/* ESP-NOW 固定信道：学生端和网关必须一致，否则互相收不到。
 * 网关那边用的是同一个值（firmware/gateway/main/gateway.c）。 */
#define ESPNOW_CHANNEL      1

/* 本节点编号（协议里的 node_id）。
 * ⚠️ 有多块学生端板子时，每块必须改成不同的值，否则网关分不清谁是谁。 */
#define MY_NODE_ID          1

/* 网关的 Wi-Fi STA MAC —— 学生端把投票发给这个地址。
 *
 * 这个值是在网关板子（ESP32-C3 SuperMini）上实测的：
 *     esptool.py --port COM38 chip_id   ->   MAC: 80:53:e0:a3:76:98
 *
 * ⚠️ 换了网关板子就必须改这里。填错不会报任何错 ——
 *    esp_now_send() 照样返回 ESP_OK，只是数据发出去没人收，很容易误判成"代码有问题"。
 *    排查方法：两块板都看串口打印的 STA MAC，确认对得上。 */
static const uint8_t GATEWAY_MAC[6] = { 0x80, 0x53, 0xE0, 0xA3, 0x76, 0x98 };

/* 板载 LED（低电平点亮），每发一次闪一下 */
#define LED_GPIO            GPIO_NUM_8

/* 发送间隔（毫秒） */
#define SEND_INTERVAL_MS    1000

/* ---------------------------------------------------------------------------
 * 与网关共用的数据结构 —— **两边必须完全一致**。
 * 对应 docs/protocol.md 第 2 节。改动时三处一起改：本文件 / gateway.c / protocol.md
 * ------------------------------------------------------------------------ */
typedef struct {
    uint8_t  node_id;
    char     vote[16];
    uint32_t sequence;
} __attribute__((packed)) vote_packet_t;

static const char *VOTE_STR[3] = { "UNDERSTAND", "UNSURE", "NOT_UNDERSTAND" };

static uint32_t s_sequence = 0;

/* ---------------------------------------------------------------------------
 * 无线初始化。刻意不用 ESP_ERROR_CHECK：失败只记警告，方便先确认串口和 LED 通路。
 * ------------------------------------------------------------------------ */
#define WIFI_TRY(expr)                                                      \
    do {                                                                    \
        esp_err_t e_ = (expr);                                              \
        if (e_ != ESP_OK) {                                                 \
            ESP_LOGW(TAG, "%s 失败: %s", #expr, esp_err_to_name(e_));       \
            return false;                                                   \
        }                                                                   \
    } while (0)

static bool wireless_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        WIFI_TRY(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init 失败: %s", esp_err_to_name(err));
        return false;
    }

    WIFI_TRY(esp_netif_init());
    WIFI_TRY(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_TRY(esp_wifi_init(&cfg));
    WIFI_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    WIFI_TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    WIFI_TRY(esp_wifi_start());
    WIFI_TRY(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    WIFI_TRY(esp_now_init());

    /* 把网关登记成 peer */
    esp_now_peer_info_t peer = { 0 };
    memcpy(peer.peer_addr, GATEWAY_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;

    if (!esp_now_is_peer_exist(GATEWAY_MAC)) {
        WIFI_TRY(esp_now_add_peer(&peer));
    }

    return true;
}

/* 发一条投票给网关 */
static void send_vote(const char *vote)
{
    vote_packet_t pkt = { 0 };
    pkt.node_id  = (uint8_t)MY_NODE_ID;
    pkt.sequence = s_sequence++;
    strncpy(pkt.vote, vote, sizeof(pkt.vote) - 1);

    esp_err_t err = esp_now_send(GATEWAY_MAC, (const uint8_t *)&pkt, sizeof(pkt));

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "已发送 -> node_id=%d vote=%s sequence=%lu",
                 pkt.node_id, pkt.vote, (unsigned long)pkt.sequence);
    } else {
        /* 注意：返回 ESP_OK 只代表"驱动收下了"，不代表对方收到了 */
        ESP_LOGW(TAG, "esp_now_send 失败: %s", esp_err_to_name(err));
    }
}

static void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(LED_GPIO, 1);   /* 低电平点亮，所以 1 = 灭 */
}

static void led_flash(void)
{
    gpio_set_level(LED_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level(LED_GPIO, 1);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ThreeKeyVoter student terminal（跳过按键，发送 Mock 投票）");

    led_init();

    /* 自己的 MAC：网关那边需要用这个地址才能回发 / 登记 peer */
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, "本机 STA MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "发送目标 MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
                 GATEWAY_MAC[0], GATEWAY_MAC[1], GATEWAY_MAC[2],
                 GATEWAY_MAC[3], GATEWAY_MAC[4], GATEWAY_MAC[5]);
        ESP_LOGI(TAG, "node_id       : %d", MY_NODE_ID);
        ESP_LOGI(TAG, "ESP-NOW 信道  : %d", ESPNOW_CHANNEL);
        ESP_LOGI(TAG, "=======================================================");
    }

    if (wireless_init()) {
        ESP_LOGI(TAG, "Wi-Fi + ESP-NOW 初始化完成");
    } else {
        ESP_LOGW(TAG, "无线初始化失败，后面发送会一直失败");
    }

    ESP_LOGI(TAG, "开始发送 Mock 投票：每 %d ms 一条", SEND_INTERVAL_MS);

    int idx = 0;
    while (true) {
        send_vote(VOTE_STR[idx % 3]);
        led_flash();
        idx++;
        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
