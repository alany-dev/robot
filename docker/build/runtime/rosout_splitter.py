#!/usr/bin/env python3

import atexit
import os
import re
import signal
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

import rosgraph
import rospy
from rosgraph_msgs.msg import Log


LEVEL_NAMES = {
    Log.DEBUG: "DEBUG",
    Log.INFO: "INFO",
    Log.WARN: "WARN",
    Log.ERROR: "ERROR",
    Log.FATAL: "FATAL",
}


class RosoutSplitter:
    def __init__(self) -> None:
        self.node_log_dir = Path(
            os.environ.get("ROBOT_NODE_LOG_DIR", Path.cwd() / "logs" / "nodes")
        )
        self.max_bytes = int(os.environ.get("ROBOT_NODE_LOG_MAX_BYTES", str(20 * 1024 * 1024)))
        self.retention_seconds = int(
            os.environ.get(
                "ROBOT_NODE_LOG_RETENTION_SECONDS",
                os.environ.get("ROBOT_LOG_RETENTION_SECONDS", "3600"),
            )
        )
        self.prune_interval_seconds = int(
            os.environ.get("ROBOT_NODE_LOG_PRUNE_INTERVAL_SECONDS", "60")
        )
        self.node_log_dir.mkdir(parents=True, exist_ok=True)
        self.handles = {}
        self.paths = {}
        self.ignored_nodes = {"/rosout_file_splitter"}
        self.last_prune_monotonic = 0.0

    @staticmethod
    def _sanitize_node_name(node_name: str) -> str:
        stripped = node_name.strip("/") or "root"
        return re.sub(r"[^A-Za-z0-9_.-]+", "__", stripped)

    @staticmethod
    def _timestamp_token() -> str:
        return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")

    @staticmethod
    def _parse_timestamp_token(token: str):
        try:
            return datetime.strptime(token, "%Y%m%d_%H%M%S_%f").replace(
                tzinfo=timezone.utc
            )
        except ValueError:
            return None

    @staticmethod
    def _format_stamp(msg: Log) -> str:
        stamp = msg.header.stamp
        if stamp.secs == 0 and stamp.nsecs == 0:
            return datetime.now(timezone.utc).isoformat()

        value = stamp.secs + stamp.nsecs / 1_000_000_000
        return datetime.fromtimestamp(value, tz=timezone.utc).isoformat()

    def _node_dir(self, node_name: str) -> Path:
        return self.node_log_dir / self._sanitize_node_name(node_name)

    def _open_new_handle(self, node_name: str):
        node_dir = self._node_dir(node_name)
        node_dir.mkdir(parents=True, exist_ok=True)
        path = node_dir / f"{self._timestamp_token()}.log"
        self.paths[node_name] = path
        self.handles[node_name] = path.open("a", encoding="utf-8")

    def _close_handle(self, node_name: str) -> None:
        handle = self.handles.pop(node_name, None)
        if handle is not None:
            handle.close()
        self.paths.pop(node_name, None)

    def _path_started_at(self, path: Path):
        parsed = self._parse_timestamp_token(path.stem)
        if parsed is not None:
            return parsed
        try:
            return datetime.fromtimestamp(path.stat().st_mtime, tz=timezone.utc)
        except FileNotFoundError:
            return None

    def _is_expired(self, path: Path, now_utc: datetime) -> bool:
        if self.retention_seconds <= 0:
            return False

        started_at = self._path_started_at(path)
        if started_at is None:
            return False

        return started_at < now_utc - timedelta(seconds=self.retention_seconds)

    def _current_size(self, node_name: str) -> int:
        path = self.paths.get(node_name)
        if path is None:
            return 0
        try:
            return path.stat().st_size
        except FileNotFoundError:
            return 0

    def _handle_for(self, node_name: str, incoming_size: int):
        handle = self.handles.get(node_name)
        if handle is None:
            self._open_new_handle(node_name)
            return self.handles[node_name]

        now_utc = datetime.now(timezone.utc)
        if self.max_bytes > 0 and self._current_size(node_name) + incoming_size > self.max_bytes:
            handle.close()
            self._open_new_handle(node_name)
        elif self._is_expired(self.paths[node_name], now_utc):
            handle.close()
            self._open_new_handle(node_name)

        return self.handles[node_name]

    def _prune_old_logs(self, force: bool = False) -> None:
        if self.retention_seconds <= 0:
            return

        now_monotonic = time.monotonic()
        if (
            not force
            and now_monotonic - self.last_prune_monotonic < self.prune_interval_seconds
        ):
            return

        self.last_prune_monotonic = now_monotonic
        now_utc = datetime.now(timezone.utc)

        for node_name, path in list(self.paths.items()):
            if self._is_expired(path, now_utc):
                self._close_handle(node_name)
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass

        for path in self.node_log_dir.glob("*/*.log"):
            if self._is_expired(path, now_utc):
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass

        for node_dir in self.node_log_dir.iterdir():
            if node_dir.is_dir():
                try:
                    next(node_dir.iterdir())
                except StopIteration:
                    node_dir.rmdir()

    def callback(self, msg: Log) -> None:
        node_name = msg.name or "/unknown"
        if node_name in self.ignored_nodes:
            return

        level = LEVEL_NAMES.get(msg.level, str(msg.level))
        timestamp = self._format_stamp(msg)

        context = []
        if msg.file:
            location = msg.file
            if msg.function:
                location = f"{location}:{msg.function}"
            if msg.line:
                location = f"{location}:{msg.line}"
            context.append(location)
        if msg.topics:
            context.append("topics=" + ",".join(msg.topics))

        suffix = f" ({' | '.join(context)})" if context else ""
        line = f"{timestamp} [{level}] {node_name}: {msg.msg}{suffix}\n"
        incoming_size = len(line.encode("utf-8"))
        handle = self._handle_for(node_name, incoming_size)
        handle.write(line)
        handle.flush()
        self._prune_old_logs()

    def close(self) -> None:
        for node_name in list(self.handles.keys()):
            self._close_handle(node_name)


def wait_for_master() -> None:
    master = rosgraph.Master("/rosout_file_splitter")

    while True:
        try:
            master.getPid()
            return
        except Exception:
            time.sleep(1)


def main() -> int:
    splitter = RosoutSplitter()

    def shutdown_handler(_signum=None, _frame=None) -> None:
        splitter.close()
        try:
            rospy.signal_shutdown("signal received")
        except Exception:
            pass

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)
    atexit.register(splitter.close)

    wait_for_master()
    rospy.init_node("rosout_file_splitter", anonymous=False, disable_signals=True)
    rospy.Subscriber("/rosout", Log, splitter.callback, queue_size=1000)
    rospy.spin()
    return 0


if __name__ == "__main__":
    sys.exit(main())
