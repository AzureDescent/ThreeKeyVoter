#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
三键投票器 · PC 后端 · B1（HTTP 接口）
=====================================

职责：从网关串口读事件 → 聚合成状态 → 用 HTTP 提供给前端。

B1 阶段只做一件事：`GET /state` 返回当前快照，前端 1 Hz 轮询即可。
（B2 换 WebSocket 推送，B3 加 Start / End 控制，B4 存历史。）

用法
----
    python server.py --serial COM5                       # 接真板子
    python server.py                                     # 自动找 ESP32 串口
    python server.py --replay tests/sample-40s.jsonl      # 没硬件时回放录制数据

接口
----
    GET /state   当前状态快照
    GET /health  存活检查

前端拿到的是"已经算好的数"，不需要自己统计。
"""

from __future__ import annotations

import argparse
import sys
import threading
import time

import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

import sources
from state import SessionState

# Windows 控制台默认可能不是 UTF-8，中文日志会乱码
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

STATE = SessionState()
_STARTED_AT = time.time()

app = FastAPI(title="ThreeKeyVoter Backend", version="0.1.0")

# 前端可能从别的端口、甚至另一台机器打开页面，B1 先全放开；
# B2 定了正式的部署方式后再收窄。
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)


@app.get("/state")
def get_state() -> dict:
    """当前状态快照。前端 1 Hz 轮询这个接口。"""
    return STATE.snapshot()


@app.get("/health")
def health() -> dict:
    return {"ok": True, "uptime": int(time.time() - _STARTED_AT)}


def _pump(events) -> None:
    """后台线程：把数据来源的事件持续喂给状态机。"""
    for event in events:
        STATE.apply(event)


def main() -> int:
    ap = argparse.ArgumentParser(description="三键投票器 PC 后端（B1：HTTP /state）")
    ap.add_argument("--serial", help="串口号，例如 COM5；不填则自动找 ESP32")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--replay", help="回放录制文件（没有硬件时用）")
    ap.add_argument("--replay-interval", type=float, default=0.05,
                    help="回放时每条事件之间的间隔（秒）")
    ap.add_argument("--host", default="127.0.0.1",
                    help="监听地址；要让别的机器访问就用 0.0.0.0")
    ap.add_argument("--port", type=int, default=8000, help="HTTP 端口")
    args = ap.parse_args()

    if args.replay:
        events = sources.iter_replay(args.replay, args.replay_interval)
    else:
        port = args.serial or sources.find_espressif_port()
        events = sources.iter_serial(port, args.baud)

    threading.Thread(target=_pump, args=(events,), daemon=True).start()
    print(f"[server] http://{args.host}:{args.port}/state", flush=True)
    uvicorn.run(app, host=args.host, port=args.port, log_level="warning")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
