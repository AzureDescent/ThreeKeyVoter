#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
三键投票器 · PC 后端 · 数据来源
================================

两种来源，产出同一种东西：串口事件 dict。

- 串口：真实网关板（需要硬件）
- 回放：录好的 JSON Lines 文件（没有硬件时也能开发和演示）

回放文件格式就是串口原始输出里以 '{' 开头的行，一行一条，例如
backend/tests/sample-40s.jsonl。
"""

from __future__ import annotations

import json
import time

ESPRESSIF_VID = 0x303A
RECONNECT_DELAY = 2.0


def parse_line(text: str) -> dict | None:
    """把一行文本解析成事件；不是数据行则返回 None。"""
    text = text.strip()
    if not text.startswith("{"):
        return None
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {"t": "__bad_json__"}


def find_espressif_port() -> str:
    """按 Espressif 的 USB VID 找串口；找不到或有多个都直接报错。"""
    from serial.tools import list_ports

    candidates = [p for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(candidates) == 1:
        return candidates[0].device
    if not candidates:
        raise SystemExit("没有找到 ESP32 串口，请用 --serial COMx 指定")
    devices = ", ".join(p.device for p in candidates)
    raise SystemExit(f"检测到多个 ESP32 串口（{devices}），请用 --serial 指定")


def iter_serial(port: str, baud: int = 115200):
    """持续读串口，断线自动重连。"""
    import serial

    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                print(f"[source] 已连接 {port} @ {baud}", flush=True)
                buf = b""
                while True:
                    chunk = ser.read(4096)
                    if not chunk:
                        continue
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        event = parse_line(line.decode("utf-8", "replace"))
                        if event is not None:
                            yield event
        except KeyboardInterrupt:
            return
        except Exception as exc:      # 串口被拔 / 被占用 / 板子复位
            print(f"[source] 串口断开：{exc}，{RECONNECT_DELAY:.0f} 秒后重连", flush=True)
            time.sleep(RECONNECT_DELAY)


def iter_replay(path: str, interval: float = 0.05):
    """按固定间隔回放录制文件，跑完即结束（服务继续用最后的状态对外提供）。"""
    print(f"[source] 回放 {path}（间隔 {interval * 1000:.0f} ms）", flush=True)
    with open(path, encoding="utf-8") as f:
        for line in f:
            event = parse_line(line)
            if event is not None:
                yield event
                time.sleep(interval)
    print("[source] 回放结束", flush=True)
