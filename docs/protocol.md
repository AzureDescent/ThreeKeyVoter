# 通信协议 v0（草案，尚未冻结）

> **状态：未冻结。**
> 这份文档只把任务书里已经出现过的字段集中到一处，方便四个方向先照着各写各的。
> 正式对接（任务书「后期接口对接」）之前都可以改；改的时候请直接更新本文件。

> ⚠️ **已有 v1 提案**：[protocol-v1-proposal.md](protocol-v1-proposal.md)。
> v1 补上了设备在线 / 离线、网关心跳与协议版本号，并取代本文第 2、3 节。
> **在 v1 冻结之前，第 2、3 节仍以本文为准**；冻结后本文会同步更新。

## 1. 投票状态取值

| 代码 | 含义 | 任务书对应 | 界面建议符号 |
| --- | --- | --- | --- |
| `UNDERSTAND` | 理解 | Button 1 | ✓ |
| `UNSURE` | 不确定 | Button 2 | ? |
| `NOT_UNDERSTAND` | 未理解 | Button 3 | × |

## 2. 学生端 → 网关（ESP-NOW）

任务书给出的字段：

```json
{
  "node_id": 1,
  "vote": "UNDERSTAND",
  "sequence": 0
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `node_id` | int | 学生端节点编号，网关靠它区分不同学生 |
| `vote` | string | 取值见上表 |
| `sequence` | int | 自增序号，用于去重与丢包检测 |

待定：时间戳、电量、RSSI（RSSI 是网关侧测得的，不一定由学生端发送）。

## 3. 网关 → PC（USB Serial）

传输格式为 **JSON Lines**：一行一条 JSON，以 `\n` 结尾，不要跨行、不要格式化缩进。

```json
{"node_id":1,"vote":"UNDERSTAND","sequence":0}
```

任务书里网关第一阶段（网关固件 第 1 节）的验收就是这个：ESP32 周期性产生 Mock Data，PC 程序稳定读取并打印。PC 端期望的终端输出形如：

```text
Node 1: UNDERSTAND
Node 2: UNSURE
```

## 4. PC → 前端

任务书给出的前端 Mock 格式（界面先按这个做）：

```json
{
  "session_id": 1,
  "devices_online": 3,
  "responses": {
    "understand": 1,
    "unsure": 1,
    "not_understand": 1
  }
}
```

> ⚠️ **注意命名不一致**：第 2 节用的是大写枚举（`UNDERSTAND`），本节用的是小写下划线（`understand`）。
> 这是任务书里就存在的差异，不是笔误。后端和前端对接时需要二选一，然后同步更新本文件。

## 5. 待定事项

- [ ] 前端与 PC 之间的传输方式：HTTP 轮询 / WebSocket / 浏览器直接读串口（Web Serial）
- [ ] `session_id` 由谁生成（PC 还是前端）
- [ ] 设备离线判定规则与断线重连策略
- [ ] 是否传输时间戳
- [ ] 节点编号如何分配（烧录时写死 / 按键配置 / 网关分配）
