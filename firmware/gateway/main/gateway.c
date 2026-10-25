/*
 * 网关 · ESP-NOW 接收 -> USB Serial 转发（协议 v1）
 * ============================================================================
 * 负责方向：网关固件
 *
 * 当前能力：
 *   1) ESP-NOW 接收学生端投票，维护节点表（在线状态 / 累计票数 / 去重）
 *   2) 按 JSON Lines 经 USB Serial 转发给 PC，共三类事件：
 *        vote —— 一张投票
 *        node —— 节点上线 / 离线
 *        gw   —— 网关心跳（含在线节点列表）
 *   3) 可选：自己产生 Mock Data（单板调试 / 验收用，见 CONFIG_GATEWAY_SELF_MOCK）
 *
 * 数据流（任务书第 2 节）：
 *   学生端 ESP32 --ESP-NOW--> 网关 ESP32 --USB Serial--> PC
 *
 * 协议细节见 docs/protocol-v1-proposal.md。
 *
 * ---------------------------------------------------------------------------
 * 串口输出格式 —— PC 端解析必须知道
 * ---------------------------------------------------------------------------
 *   本工程同时往 USB Serial 写两类内容：
 *     1. ESP-IDF 日志行：形如  I (1234) gateway: ...
 *     2. 数据行：以 '{' 开头的纯 JSON，以 '\n' 结尾
 *
 *   PC 端解析的正确做法：**只处理以 '{' 开头的行，其余一律跳过。**
 *   解析器遇到不认识的 "t" 取值应当跳过而不是报错退出 —— 这是为协议继续
 *   演进留的余地。
 *
 * ---------------------------------------------------------------------------
 * 关于拼 JSON
 * ---------------------------------------------------------------------------
 *   ESP-IDF v6 起 cJSON 已被移出 IDF、变成需要联网拉取的托管组件。
 *   这里要拼的 JSON 都很简单，直接 snprintf 手拼，不要引入 cJSON。
 * ============================================================================
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"

static const char *TAG = "gateway";

/* ===========================================================================
 * 编译开关：网关要不要自己造假数据
 * ---------------------------------------------------------------------------
 *   由 Kconfig 提供（idf.py menuconfig -> ThreeKeyVoter Gateway）
 *   0 = 只转发 ESP-NOW 收到的真实数据（默认，两块板联调时用这个）
 *   1 = 自己周期性产生 Mock Data，但**走与真实链路完全相同的处理路径**，
 *       因此在只有一块板时也能验证节点表 / 去重 / 在线判定 / 心跳
 * =========================================================================== */
#ifdef CONFIG_GATEWAY_SELF_MOCK
#define GATEWAY_SELF_MOCK   1
#else
#define GATEWAY_SELF_MOCK   0
#endif

/* ESP-NOW 固定信道：所有节点必须一致，否则互相收不到 */
#define ESPNOW_CHANNEL      1

/* 协议版本。学生端 / 网关 / 文档三处必须一致 */
#define PROTOCOL_VERSION    1

/* 节点表容量。一块网关同时支持的节点数上限 */
#define MAX_NODES           8

/* 超过这个时间没收到该节点的任何包，判定离线 */
#define NODE_TIMEOUT_MS     5000

/* 网关心跳间隔 */
#define GW_HEARTBEAT_MS     1000

/* Mock 参数（仅 GATEWAY_SELF_MOCK=1 时有效） */
#define MOCK_NODE_COUNT     3
#define MOCK_INTERVAL_MS    1000
/* Mock 模式下让第 3 个节点在 MOCK_DROPOUT_MS 后停发、MOCK_RESUME_MS 后恢复，
 * 用来在同一次运行里验证"离线"和"恢复上线"两类事件 */
#define MOCK_DROPOUT_MS     20000
#define MOCK_RESUME_MS      30000

/* 投票取值，与 docs/protocol.md 第 1 节的顺序一致 */
#define VOTE_UNDERSTAND         0
#define VOTE_UNSURE             1
#define VOTE_NOT_UNDERSTAND     2
#define VOTE_HEARTBEAT          3

static const char *VOTE_STR[3] = {
    "UNDERSTAND",
    "UNSURE",
    "NOT_UNDERSTAND",
};

/* 与网关共用的数据结构 —— **两边必须完全一致**。
 * 改动时三处一起改：gateway.c / student.c / docs/protocol-v1-proposal.md
 * 必须 packed：否则编译器会插入对齐填充，导致两端算出的长度不同、字段错位。 */
typedef struct {
    uint8_t  ver;       /* 协议版本，当前 = 1 */
    uint8_t  node_id;   /* 1..MAX_NODES */
    uint8_t  vote;      /* VOTE_* 之一 */
    uint32_t sequence;  /* 节点自增序号 */
} __attribute__((packed)) vote_packet_t;

typedef struct {
    bool     used;
    uint8_t  node_id;
    uint8_t  mac[6];
    uint32_t last_seen_ms;
    uint32_t count;      /* 累计收到的投票数（不含心跳） */
    uint32_t last_seq;
    bool     online;
} node_t;

static node_t s_nodes[MAX_NODES];
static uint32_t s_rx_total = 0;      /* 有效投票总数 */
static uint32_t s_dedup_drop = 0;    /* 因 sequence 重复被丢弃的包 */
static uint32_t s_bad_packet = 0;    /* 长度 / 版本 / 取值非法的包 */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 输出一行，并立刻 flush：串口不能用行缓冲，否则 PC 端会延迟看到数据 */
static void write_line(const char *line)
{
    fwrite(line, 1, strlen(line), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void emit_vote(uint8_t node_id, uint8_t vote, uint32_t seq, int rssi)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"vote\",\"node\":%u,\"vote\":\"%s\",\"seq\":%lu,\"rssi\":%d}",
             (unsigned)node_id, VOTE_STR[vote], (unsigned long)seq, rssi);
    write_line(buf);
}

static void emit_node(uint8_t node_id, bool online, uint32_t count)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"node\",\"node\":%u,\"online\":%s,\"count\":%lu}",
             (unsigned)node_id, online ? "true" : "false", (unsigned long)count);
    write_line(buf);
}

static void emit_gw(uint32_t uptime_ms)
{
    char list[64];
    size_t pos = 0;
    bool first = true;

    list[pos++] = '[';
    for (int i = 0; i < MAX_NODES; i++) {
        if (!s_nodes[i].used || !s_nodes[i].online) {
            continue;
        }
        int n = snprintf(&list[pos], sizeof(list) - pos, "%s%u",
                         first ? "" : ",", (unsigned)s_nodes[i].node_id);
        if (n <= 0 || (size_t)n >= sizeof(list) - pos) {
            break;
        }
        pos += (size_t)n;
        first = false;
    }
    if (pos < sizeof(list) - 1) {
        list[pos++] = ']';
    }
    list[pos] = '\0';

    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"gw\",\"uptime\":%lu,\"rx\":%lu,\"drop\":%lu,\"bad\":%lu,\"online\":%s,\"ch\":%d}",
             (unsigned long)uptime_ms, (unsigned long)s_rx_total,
             (unsigned long)s_dedup_drop, (unsigned long)s_bad_packet,
             list, ESPNOW_CHANNEL);
    write_line(buf);
}

static node_t *node_find(uint8_t node_id)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].node_id == node_id) {
            return &s_nodes[i];
        }
    }
    return NULL;
}

static node_t *node_get_or_create(uint8_t node_id, const uint8_t *mac)
{
    node_t *n = node_find(node_id);
    if (n != NULL) {
        /* 同一 node_id 换了 MAC：多半是改了节点编号或换了板子，记一条警告 */
        if (mac != NULL && memcmp(n->mac, mac, 6) != 0) {
            ESP_LOGW(TAG, "node_id %u 的 MAC 变了", (unsigned)node_id);
            memcpy(n->mac, mac, 6);
        }
        return n;
    }

    for (int i = 0; i < MAX_NODES; i++) {
        if (!s_nodes[i].used) {
            memset(&s_nodes[i], 0, sizeof(node_t));
            s_nodes[i].used = true;
            s_nodes[i].node_id = node_id;
            if (mac != NULL) {
                memcpy(s_nodes[i].mac, mac, 6);
            }
            s_nodes[i].online = false;
            return &s_nodes[i];
        }
    }

    ESP_LOGW(TAG, "节点表已满（%d 个），忽略 node_id %u", MAX_NODES, (unsigned)node_id);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * 所有投票的处理入口。ESP-NOW 回调和 Mock 产生器都走这里，
 * 这样 Mock 模式能真正验证后面整套逻辑，而不是只验证 printf。
 * ------------------------------------------------------------------------ */
static void handle_packet(const uint8_t *data, int len, const uint8_t *mac, int rssi)
{
    if (len < (int)sizeof(vote_packet_t)) {
        s_bad_packet++;
        ESP_LOGW(TAG, "数据长度异常：%d 字节（期望 %d）", len, (int)sizeof(vote_packet_t));
        return;
    }

    vote_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    if (pkt.ver != PROTOCOL_VERSION) {
        s_bad_packet++;
        ESP_LOGW(TAG, "协议版本不匹配：收到 %u，期望 %u", (unsigned)pkt.ver, PROTOCOL_VERSION);
        return;
    }
    if (pkt.node_id == 0 || pkt.node_id > MAX_NODES) {
        s_bad_packet++;
        ESP_LOGW(TAG, "node_id 越界：%u", (unsigned)pkt.node_id);
        return;
    }
    if (pkt.vote > VOTE_HEARTBEAT) {
        s_bad_packet++;
        ESP_LOGW(TAG, "vote 取值非法：%u", (unsigned)pkt.vote);
        return;
    }

    node_t *n = node_get_or_create(pkt.node_id, mac);
    if (n == NULL) {
        return;
    }

    uint32_t t = now_ms();

    /* 去重：同一个 sequence 只认一次（心跳也带 sequence，同样参与去重） */
    if (n->count > 0 && pkt.sequence == n->last_seq) {
        s_dedup_drop++;
        ESP_LOGW(TAG, "重复包：node_id=%u seq=%lu", (unsigned)pkt.node_id, (unsigned long)pkt.sequence);
        return;
    }

    n->last_seq = pkt.sequence;
    n->last_seen_ms = t;

    if (!n->online) {
        n->online = true;
        emit_node(pkt.node_id, true, n->count);
        ESP_LOGI(TAG, "节点 %u 上线", (unsigned)pkt.node_id);
    }

    if (pkt.vote == VOTE_HEARTBEAT) {
        return;   /* 心跳只用来刷新在线状态，不算投票 */
    }

    n->count++;
    s_rx_total++;
    emit_vote(pkt.node_id, pkt.vote, pkt.sequence, rssi);
}

/* ---------------------------------------------------------------------------
 * ESP-NOW 接收回调
 * ------------------------------------------------------------------------ */
static void on_espnow_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    const uint8_t *src = info->src_addr;
    int rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    handle_packet(data, len, src, rssi);
}

/* 扫描节点表，把超时的节点标记为离线并上报一次 */
static void check_offline(void)
{
    uint32_t t = now_ms();
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].online &&
            (t - s_nodes[i].last_seen_ms) > NODE_TIMEOUT_MS) {
            s_nodes[i].online = false;
            emit_node(s_nodes[i].node_id, false, s_nodes[i].count);
            ESP_LOGI(TAG, "节点 %u 离线（%lu ms 无数据）",
                     (unsigned)s_nodes[i].node_id, (unsigned long)(t - s_nodes[i].last_seen_ms));
        }
    }
}

/* ---------------------------------------------------------------------------
 * 无线初始化。刻意不用 ESP_ERROR_CHECK：失败只记警告，方便定位问题。
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
    WIFI_TRY(esp_now_register_recv_cb(on_espnow_recv));

    return true;
}

#if GATEWAY_SELF_MOCK
static uint32_t s_mock_seq[MOCK_NODE_COUNT];
static uint32_t s_mock_start_ms;

static void mock_tick(void)
{
    uint32_t elapsed = now_ms() - s_mock_start_ms;

    for (int i = 0; i < MOCK_NODE_COUNT; i++) {
        /* 最后一个节点在演示窗口内停发，用来演示"离线"与"恢复上线" */
        if (i == (MOCK_NODE_COUNT - 1) &&
            elapsed > MOCK_DROPOUT_MS && elapsed < MOCK_RESUME_MS) {
            continue;
        }

        vote_packet_t pkt;
        pkt.ver      = PROTOCOL_VERSION;
        pkt.node_id  = (uint8_t)(i + 1);
        pkt.vote     = (uint8_t)(esp_random() % 3);
        pkt.sequence = s_mock_seq[i]++;

        /* 伪造一个本机 MAC，让节点表 / 日志路径与真实链路一致 */
        uint8_t fake_mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, (uint8_t)(i + 1) };
        int fake_rssi = -(40 + i * 3);
        handle_packet((const uint8_t *)&pkt, (int)sizeof(pkt), fake_mac, fake_rssi);
    }
}
#endif

void app_main(void)
{
#if GATEWAY_SELF_MOCK
    ESP_LOGI(TAG, "ThreeKeyVoter gateway —— 自产 Mock Data 模式（走完整处理路径）");
#else
    ESP_LOGI(TAG, "ThreeKeyVoter gateway —— ESP-NOW 接收模式");
#endif

    /* MAC 先打出来：学生端要发给这个地址，且必须用同一信道。 */
    uint8_t mac[6] = { 0 };
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, "Wi-Fi STA MAC : %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "ESP-NOW 信道  : %d", ESPNOW_CHANNEL);
        ESP_LOGI(TAG, "协议版本      : %d", PROTOCOL_VERSION);
        ESP_LOGI(TAG, "(学生端的 GATEWAY_MAC 必须填上面这个地址)");
        ESP_LOGI(TAG, "=======================================================");
    } else {
        ESP_LOGW(TAG, "读取 MAC 失败");
    }

#if !GATEWAY_SELF_MOCK
    if (wireless_init()) {
        ESP_LOGI(TAG, "Wi-Fi + ESP-NOW 初始化完成，等待学生端数据…");
    } else {
        ESP_LOGW(TAG, "无线初始化失败");
    }
#endif

    ESP_LOGI(TAG, "PC 端解析规则：只处理以 '{' 开头的行，按 t 字段分发");

#if GATEWAY_SELF_MOCK
    s_mock_start_ms = now_ms();
    ESP_LOGI(TAG, "Mock：每秒产生 %d 条投票；第 %d 号节点在 %d–%d ms 停发，用于演示离线/恢复",
             MOCK_NODE_COUNT, MOCK_NODE_COUNT, MOCK_DROPOUT_MS, MOCK_RESUME_MS);
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(GW_HEARTBEAT_MS));

#if GATEWAY_SELF_MOCK
        mock_tick();
#endif
        check_offline();
        emit_gw(now_ms());
    }
}
