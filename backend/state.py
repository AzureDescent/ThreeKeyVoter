#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
三键投票器 · PC 后端 · 状态聚合
================================

把串口上报的三类事件（vote / node / gw）聚合成前端要的一份状态。

这一层是纯逻辑：不碰串口、不碰网络，所以可以单独跑测试
（见 tests/test_state.py）。

协议见 docs/protocol-v1-proposal.md。
"""

from __future__ import annotations

import time
from datetime import datetime, timedelta, timezone

# 串口层用大写枚举，HTTP 层用小写 —— 映射只在这里做一次
VOTE_KEYS = {
    "UNDERSTAND": "understand",
    "UNSURE": "unsure",
    "NOT_UNDERSTAND": "not_understand",
}

# 超过这么久没收到 gw 心跳，就认为网关掉线
GATEWAY_TIMEOUT_S = 3.0

TZ = timezone(timedelta(hours=8))


class SessionState:
    """一次课堂反馈的完整状态。"""

    def __init__(self) -> None:
        self.session_id = 1
        self.status = "idle"            # idle | voting | ended
        self._nodes: dict[int, dict] = {}
        self._responses = {k: 0 for k in VOTE_KEYS.values()}
        self._gateway_last_seen = 0.0
        self._updated_at = 0.0
        self._dropped = {"bad": 0, "unknown": 0, "after_end": 0}

    # ---------- 写入 ----------
    def apply(self, event: dict, now: float | None = None) -> None:
        """吃进一条串口事件。未知事件只计数，不抛异常（向前兼容）。"""
        now = time.time() if now is None else now
        kind = event.get("t")

        if kind == "vote":
            self._apply_vote(event, now)
        elif kind == "node":
            self._apply_node(event, now)
        elif kind == "gw":
            self._gateway_last_seen = now
            self._updated_at = now
        else:
            self._dropped["unknown"] += 1

    def _node(self, node_id: int) -> dict:
        return self._nodes.setdefault(node_id, {"online": False, "count": 0})

    def _apply_vote(self, event: dict, now: float) -> None:
        # 先校验取值：坏数据无论什么状态都算 bad，不应混进 after_end
        key = VOTE_KEYS.get(event.get("vote"))
        if key is None:
            self._dropped["bad"] += 1
            return

        if self.status == "ended":
            # 点了 End 之后到达的票不计数，保证定格结果不被污染
            self._dropped["after_end"] += 1
            return

        if self.status == "idle":
            # 第一张票自动开启 session（B1 没有显式 Start，B3 再加）
            self.status = "voting"

        node_id = event.get("node")
        if node_id is not None:
            rec = self._node(int(node_id))
            rec["online"] = True
            rec["count"] += 1        # 网关的 node 事件会带权威计数并覆盖它

        self._responses[key] += 1
        self._updated_at = now

    def _apply_node(self, event: dict, now: float) -> None:
        node_id = event.get("node")
        if node_id is None:
            self._dropped["bad"] += 1
            return
        rec = self._node(int(node_id))
        rec["online"] = bool(event.get("online"))
        if event.get("count") is not None:
            rec["count"] = int(event["count"])
        self._updated_at = now

    # ---------- 控制（B3 的 Start / End 会用到） ----------
    def start_session(self, now: float | None = None) -> None:
        now = time.time() if now is None else now
        if self.status != "idle":
            self.session_id += 1
        self.status = "voting"
        self._responses = {k: 0 for k in VOTE_KEYS.values()}
        for rec in self._nodes.values():
            rec["count"] = 0
        self._updated_at = now

    def end_session(self, now: float | None = None) -> None:
        self.status = "ended"
        self._updated_at = time.time() if now is None else now

    # ---------- 读取 ----------
    def snapshot(self, now: float | None = None) -> dict:
        now = time.time() if now is None else now
        online = sum(1 for r in self._nodes.values() if r["online"])
        gateway_online = (
            self._gateway_last_seen > 0
            and (now - self._gateway_last_seen) < GATEWAY_TIMEOUT_S
        )
        ts = self._updated_at or now
        return {
            "session_id": self.session_id,
            "state": self.status,
            "devices_online": online,
            "responses": dict(self._responses),
            "total": sum(self._responses.values()),
            "gateway_online": gateway_online,
            "nodes": [
                {"node": n, "online": r["online"], "count": r["count"]}
                for n, r in sorted(self._nodes.items())
            ],
            "dropped": dict(self._dropped),
            "updated_at": datetime.fromtimestamp(ts, tz=TZ).isoformat(timespec="seconds"),
        }
