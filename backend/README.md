# backend — PC 端程序

**负责方向：PC 后端**（前期的串口读取由网关固件侧打通）

## 这一层要做什么

1. 从网关 ESP32 的串口读数据（本机为 `COM38`，JSON Lines 格式）
2. 解析成结构化数据
3. 通过 HTTP / WebSocket 提供给前端
4. （后期）保存 session 历史，供课后回看

```text
网关 ESP32 ──USB Serial──▶ backend ──HTTP / WebSocket──▶ frontend
```

通信格式见 [../docs/protocol.md](../docs/protocol.md)。

## 对应任务书

- 网关固件 第 1 节：ESP32 → PC Demo，先证明 PC 通路工作
- 网关固件 第 2 节 + 前端 第 4 节：真正的 Gateway 与接口对接

## 现状

目录刚建，**以下都还没定**：

- 技术选型（Node / Python）
- 前端是「连本地后端」还是「用 Web Serial 直接读串口」——若选后者，本目录可能不需要
- 启动方式与端口

定了之后请在这里补上运行说明。
