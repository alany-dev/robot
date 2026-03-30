#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path


def sanitize_name(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name.strip() or "process")


def timestamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")


class RotatingLogFile:
    def __init__(self, log_dir: Path, name: str, max_bytes: int, retention_seconds: int) -> None:
        self.log_dir = log_dir
        self.name = sanitize_name(name)
        self.max_bytes = max_bytes
        self.retention_seconds = retention_seconds
        self.current_path = self.log_dir / f"{self.name}.log"
        self.archive_dir = self.log_dir / "archive" / self.name
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.archive_dir.mkdir(parents=True, exist_ok=True)
        self.current_token = timestamp()
        self.handle = self.current_path.open("ab")
        self.last_prune_monotonic = 0.0

    def _current_size(self) -> int:
        try:
            return self.current_path.stat().st_size
        except FileNotFoundError:
            return 0

    def _should_rotate(self, incoming_size: int) -> bool:
        if self.max_bytes <= 0:
            return False

        current_size = self._current_size()
        return current_size + incoming_size > self.max_bytes

    @staticmethod
    def _parse_token(path: Path):
        try:
            return datetime.strptime(path.stem, "%Y%m%d_%H%M%S_%f").replace(
                tzinfo=timezone.utc
            )
        except ValueError:
            return None

    def _current_started_at(self):
        return self._parse_token(Path(f"{self.current_token}.log"))

    def _should_rotate_for_age(self) -> bool:
        if self.retention_seconds <= 0:
            return False

        started_at = self._current_started_at()
        if started_at is None:
            return False

        return started_at < datetime.now(timezone.utc) - timedelta(
            seconds=self.retention_seconds
        )

    def _rotate(self) -> None:
        self.handle.close()

        if self.current_path.exists() and self.current_path.stat().st_size > 0:
            archived_path = self.archive_dir / f"{self.current_token}.log"
            shutil.move(str(self.current_path), str(archived_path))

        self.current_token = timestamp()
        self.handle = self.current_path.open("ab")
        self._prune_old_archives(force=True)

    def _prune_old_archives(self, force: bool = False) -> None:
        if self.retention_seconds <= 0:
            return

        now_monotonic = time.monotonic()
        if not force and now_monotonic - self.last_prune_monotonic < 60:
            return

        self.last_prune_monotonic = now_monotonic
        cutoff = datetime.now(timezone.utc) - timedelta(seconds=self.retention_seconds)

        for path in self.archive_dir.glob("*.log"):
            started_at = self._parse_token(path)
            if started_at is not None and started_at < cutoff:
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass

    def write(self, chunk: bytes) -> None:
        if not chunk:
            return

        if self._should_rotate_for_age():
            self._rotate()

        if self.max_bytes <= 0:
            self.handle.write(chunk)
            self.handle.flush()
            self._prune_old_archives()
            return

        offset = 0
        total = len(chunk)
        while offset < total:
            current_size = self._current_size()
            remaining = self.max_bytes - current_size
            if remaining <= 0:
                self._rotate()
                current_size = self._current_size()
                remaining = self.max_bytes - current_size

            if remaining <= 0:
                remaining = self.max_bytes

            next_offset = min(offset + remaining, total)
            self.handle.write(chunk[offset:next_offset])
            self.handle.flush()
            offset = next_offset

            if offset < total and self._should_rotate(0):
                self._rotate()

        self._prune_old_archives()

    def close(self) -> None:
        self.handle.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--max-bytes", type=int, default=20 * 1024 * 1024)
    parser.add_argument(
        "--retention-seconds",
        type=int,
        default=int(
            os.environ.get(
                "ROBOT_PROCESS_LOG_RETENTION_SECONDS",
                os.environ.get("ROBOT_LOG_RETENTION_SECONDS", "3600"),
            )
        ),
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if args.command and args.command[0] == "--":
        args.command = args.command[1:]

    if not args.command:
        parser.error("missing command to execute")

    return args


def main() -> int:
    args = parse_args()
    log_file = RotatingLogFile(
        log_dir=Path(args.log_dir),
        name=args.name,
        max_bytes=args.max_bytes,
        retention_seconds=args.retention_seconds,
    )
    child = None

    def handle_signal(signum, _frame) -> None:
        if child is not None and child.poll() is None:
            child.terminate()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    try:
        env = os.environ.copy()
        child = subprocess.Popen(
            args.command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        )

        assert child.stdout is not None
        while True:
            chunk = child.stdout.read1(65536)
            if not chunk:
                break
            log_file.write(chunk)

        return child.wait()
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        log_file.close()


if __name__ == "__main__":
    sys.exit(main())
