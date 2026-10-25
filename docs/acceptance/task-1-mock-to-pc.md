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
| 6 | 拔插 USB 后自动重连 | ✅ 见 §4.2（物理拔插 + 重连分支双验证） |
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

## 4.2 拔插 USB 自动重连（判据 6）

分两步验证，因为「物理事件」和「程序逻辑」是两个独立的点。

### 第一步：程序侧重连分支（模拟）

把 `read_serial.py` 的 `serial.Serial` 替换成假实现：前几个 `read()` 正常返回数据，之后抛
`SerialException`（模拟设备被拔出），再之后正常返回。实测输出：

```text
已连接 COM_TEST @ 115200，等待数据…（Ctrl+C 退出）
Node 1: UNDERSTAND
Node 1: UNDERSTAND
串口断开：[Errno 2] 模拟：设备已拔出
0 秒后重连…（Ctrl+C 退出）
已连接 COM_TEST @ 115200，等待数据…（Ctrl+C 退出）
Node 2: UNSURE
Node 2: UNSURE
```

断开能捕获、能自动重开端口、重连后统计继续累积（`ok` 累计 5 条）。说明重连分支正确。

### 第二步：物理拔插

用检测脚本盯着 `COM5`，记录「串口消失 → 重新枚举 → 数据恢复」三个事件：

```json
{
  "disconnect_detected": true,
  "reconnect_detected": true,
  "votes_before": 321,
  "votes_after": 3,
  "reopen_attempts": 25,
  "events_after": ["port reopened", "node event x3 after reconnect"]
}
```

拔掉后串口消失（检测到），拔出期间持续重试打开（25 次），插回后重新枚举成功、数据恢复，
并且收到 3 条「节点上线」事件 —— 说明板子确实重新上电、从零开始，不是假断开。

> 说明：第二步用的是等价检测脚本（同一套读取语义），不是直接跑 `read_serial.py`；
> 第一步已经单独验证了 `read_serial.py` 的重连分支。两点合起来判定通过。

## 5. 结论

任务书「网关固件」第 0 节（能编译烧录 ESP32-C3、能用 Git 管理代码）与第 1 节（ESP32 周期性产生 Mock Data，电脑程序稳定读取并打印）的验收判据 **7 条全部达成**。

## 6. 遗留与下一步

1. Mock 开关已改为 Kconfig 选项（见 `docs/protocol-v1-proposal.md`），不再是源码改动。
2. 下一步：协议 v1 冻结 → 后端 B1（`GET /state`）→ 结构尺寸实测。
