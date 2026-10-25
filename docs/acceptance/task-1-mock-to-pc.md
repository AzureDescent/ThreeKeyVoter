# 验收记录 · 任务 1：ESP32 → PC

| 项目 | 内容 |
| --- | --- |
| 验收人 | 郭昭岩（`Zhaoyan-1116`） |
| 日期 | 2026-10-25 |
| 对应任务书 | 「网关固件 + 结构」第 0 节、第 1 节 |
| 仓库版本 | `AzureDescent/ThreeKeyVoter` @ `539c112` |

## 1. 环境与设备

| 项 | 值 |
| --- | --- |
| 开发板 | ESP32-C3 SuperMini（芯片 ESP32-C3 QFN32 rev v0.4，4 MB XMC Flash，40 MHz 晶振） |
| USB 模式 | USB-Serial/JTAG（原生 USB），枚举为 `COM5` |
| 板子 MAC | `20:d5:c2:0f:66:40` |
| ESP-IDF | v6.1 |
| Python | 3.14.3，`pyserial` 3.5 |
| 编译产物 | `gateway.bin` 463.30 KB，最小 app 分区 1536 KB，剩余 49% |

## 2. 本次验收用的配置

固件侧：打开 Mock 开关（`sdkconfig` 不入库，属本地配置）

```powershell
idf.py menuconfig     # ThreeKeyVoter Gateway -> 网关自产 Mock 数据
```

> 当时的实现是源码里的 `#define GATEWAY_SELF_MOCK 1`；后续已改成 Kconfig 选项，见 `docs/protocol-v1-proposal.md`。

PC 侧：

```powershell
python backend\read_serial.py --port COM5
```

## 3. 判据与结果

| # | 判据 | 结果 |
| --- | --- | --- |
| 1 | 固件启动打印 `Wi-Fi STA MAC` | ✅ 串口日志可见 |
| 2 | 每秒出现 3 行 `Node N: VOTE` | ✅ 实测 3.00 条/秒 |
| 3 | 连续 5 分钟收到 ≥ 800 条 | ✅ **900 条**（300 秒 × 3 条/秒） |
| 4 | 解析失败 0 条、未知 vote 0 条 | ✅ 0 条失败 |
| 5 | `node_id` 1/2/3 都出现，三种 vote 都出现 | ✅ 每节点各 300 条 |
| 6 | 拔插 USB 后 ≤ 2 秒自动重连 | ⬜ 未实测（需物理拔插） |
| 7 | `--json` 输出可被消费 | ✅ 15 行全部可 `JSON.parse` |

## 4. 现场输出

5 分钟连续抓取的前 10 行：

```text
Node 1: UNDERSTAND
Node 2: UNSURE
Node 3: NOT_UNDERSTAND
Node 1: UNSURE
Node 2: UNDERSTAND
Node 3: UNDERSTAND
Node 1: UNDERSTAND
Node 2: NOT_UNDERSTAND
Node 3: NOT_UNDERSTAND
Node 1: UNDERSTAND
```

5 分钟统计：

| 指标 | 数值 |
| --- | --- |
| 合法数据行 | 900 |
| 其它行 | 1（启动提示「已连接 COM5 @ 115200，等待数据…」） |
| 速率 | 3.00 条/秒 |
| `UNDERSTAND` | 287 |
| `UNSURE` | 320 |
| `NOT_UNDERSTAND` | 293 |
| node_id = 1 / 2 / 3 | 300 / 300 / 300 |
| JSON 解析失败 | 0 |

`--json` 模式样例：

```json
{"node_id": 1, "vote": "NOT_UNDERSTAND", "sequence": 11}
{"node_id": 2, "vote": "UNSURE", "sequence": 11}
```

## 4.1 复测（协议 v1 代码，2026-10-25 晚）

网关固件升级到 v1（节点表 / 心跳 / 去重 / 版本号）后，用同样的方法重跑了一遍 5 分钟抓取：

| 指标 | v0 代码 | v1 代码 |
| --- | --- | --- |
| 合法投票行 | 900 | 900 |
| 速率 | 3.00 条/秒 | 3.00 条/秒 |
| node_id 1 / 2 / 3 | 300 / 300 / 300 | 300 / 300 / 300 |
| JSON 解析失败 | 0 | 0 |
| 编译告警 | — | 0 |

结论：v1 改动没有影响投票链路。

## 5. 结论

任务书「网关固件」第 0 节（能编译烧录 ESP32-C3、能用 Git 管理代码）与第 1 节（ESP32 周期性产生 Mock Data，电脑程序稳定读取并打印）的验收判据全部达成。第 6 条留待下次物理拔插时补测。

## 6. 遗留与下一步

1. `GATEWAY_SELF_MOCK=1` 是本地改动：提交前改回 0，或改造成编译期开关（`menuconfig` / `-D`）。
2. 第 6 条判据未测。
3. 下一步：协议 v1 提案 → 网关节点表与心跳 → 后端 B1。
