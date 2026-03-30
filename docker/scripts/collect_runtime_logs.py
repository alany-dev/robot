#!/usr/bin/env python3

import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, List, Tuple


PROJECT_ROOT = Path(__file__).resolve().parents[2]
LOG_ROOT = PROJECT_ROOT / "logs"
AUDIT_ROOT = LOG_ROOT / "runtime_audit"
DOCKER_LOG_ROOT = LOG_ROOT / "docker"
NODE_LOG_ROOT = Path("/home/orangepi/robot/logs/nodes")
DEFAULT_CONTAINER = os.environ.get("ROBOT_CONTAINER_NAME", "robot")


ROS_SHELL_PREFIX = """
source /opt/ros/noetic/setup.bash >/dev/null 2>&1 || true
if [ -f /home/orangepi/robot/devel/setup.bash ]; then
  source /home/orangepi/robot/devel/setup.bash >/dev/null 2>&1 || true
fi
export ROS_MASTER_URI="${ROS_MASTER_URI:-http://127.0.0.1:11311}"
if [ -z "${ROS_IP:-}" ]; then
  unset ROS_IP
fi
if [ -z "${ROS_HOSTNAME:-}" ]; then
  unset ROS_HOSTNAME
fi
""".strip()


@dataclass(frozen=True)
class ExpectedComponent:
    name: str
    ros_node: str
    process_pattern: str
    binary_path: str


EXPECTED_COMPONENTS: Tuple[ExpectedComponent, ...] = (
    ExpectedComponent("robot_state_publisher", "/robot_state_publisher", "robot_state_publisher", "/opt/ros/noetic/lib/robot_state_publisher/robot_state_publisher"),
    ExpectedComponent("usb_camera", "/usb_camera", "/home/orangepi/robot/devel/lib/usb_camera/usb_camera", "/home/orangepi/robot/devel/lib/usb_camera/usb_camera"),
    ExpectedComponent("img_decode", "/img_decode", "/home/orangepi/robot/devel/lib/img_decode/img_decode", "/home/orangepi/robot/devel/lib/img_decode/img_decode"),
    ExpectedComponent("rknn_yolov6", "/rknn_yolov6", "/home/orangepi/robot/devel/lib/rknn_yolov6/rknn_yolov6", "/home/orangepi/robot/devel/lib/rknn_yolov6/rknn_yolov6"),
    ExpectedComponent("img_encode", "/img_encode", "/home/orangepi/robot/devel/lib/img_encode/img_encode", "/home/orangepi/robot/devel/lib/img_encode/img_encode"),
    ExpectedComponent("ydlidar_node", "/ydlidar_node", "ydlidar_node", "/home/orangepi/robot/devel/lib/ydlidar/ydlidar_node"),
    ExpectedComponent("object_track", "/object_track", "/home/orangepi/robot/devel/lib/object_track/object_track", "/home/orangepi/robot/devel/lib/object_track/object_track"),
)


def run_command(args: List[str]) -> subprocess.CompletedProcess:
    return subprocess.run(args, text=True, capture_output=True)


def write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def store_completed(path: Path, completed: subprocess.CompletedProcess) -> None:
    parts = [
        f"$ {' '.join(completed.args)}\n",
        f"[exit_code] {completed.returncode}\n\n",
        "[stdout]\n",
        completed.stdout or "",
        "\n[stderr]\n",
        completed.stderr or "",
    ]
    write_file(path, "".join(parts))


def safe_lines(text: str) -> List[str]:
    return [line for line in text.splitlines() if line.strip()]


def relevant_ros_nodes(text: str) -> List[str]:
    return [line for line in safe_lines(text) if not line.startswith("/rostopic_")]


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)


def ros_log_matches(component_name: str, tree_text: str) -> List[str]:
    return [line for line in safe_lines(tree_text) if component_name in Path(line).name]


def node_log_paths(tree_text: str) -> List[str]:
    return safe_lines(tree_text)


def node_log_display_name(path_text: str) -> str:
    path = Path(path_text)
    try:
        return str(path.relative_to(NODE_LOG_ROOT))
    except ValueError:
        return path.name


def node_log_matches(component_name: str, tree_text: str) -> List[str]:
    component_name = component_name.lower()
    return [
        line for line in node_log_paths(tree_text)
        if component_name in node_log_display_name(line).lower()
    ]


def bool_mark(value: bool) -> str:
    return "yes" if value else "no"


def extract_launch_errors(process_log_tail: str) -> List[str]:
    return [strip_ansi(line).strip() for line in process_log_tail.splitlines() if "ERROR:" in line]


def env_flag(env_text: str, key: str, default: str = "unset") -> str:
    prefix = f"{key}="
    for line in safe_lines(env_text):
        if line.startswith(prefix):
            return line[len(prefix):]
    return default


def ensure_latest_symlink(target: Path) -> None:
    latest = AUDIT_ROOT / "latest"
    if latest.exists() or latest.is_symlink():
        latest.unlink()
    latest.symlink_to(target.name)


def ensure_file_symlink(link_path: Path, target_path: Path) -> None:
    if link_path.exists() or link_path.is_symlink():
        link_path.unlink()
    link_path.symlink_to(os.path.relpath(target_path, link_path.parent))


def write_logs_index(
    snapshot_dir: Path,
    active_ros_nodes: List[str],
    node_log_names: List[str],
    container_log_name: str,
    launch_errors: List[str],
) -> None:
    start_here = LOG_ROOT / "00_START_HERE.md"
    current_summary = LOG_ROOT / "01_CURRENT_SUMMARY.md"
    ensure_file_symlink(current_summary, AUDIT_ROOT / "latest" / "SUMMARY.md")

    lines = [
        "# Logs Guide",
        "",
        "Open files in this order:",
        "",
        "1. `01_CURRENT_SUMMARY.md`",
        "   The current runtime audit. Start here every time.",
        "2. `nodes/`",
        "   Per-node `/rosout` logs. Use this when a ROS node is running and you want node-level messages.",
        f"3. `docker/{container_log_name}`",
        "   Container stdout/stderr. Use this for startup failures, build failures, web, and agent logs.",
        "",
        "Ignore by default:",
        "",
        "- Old timestamped folders under `runtime_audit/`. Use only when comparing history.",
        "- Older timestamped files under each node directory. Open the newest one first.",
        "",
        "What to open for common problems:",
        "",
        f"- Container startup / RK rebuild / catkin_make failed: `docker/{container_log_name}`",
        "- Running ROS node issue: the newest file under `nodes/<node_name>/`",
        f"- Agent or web controller issue: `docker/{container_log_name}`",
        "- Base control runtime warnings: the newest file under `nodes/base_control/`",
        "",
        f"Current snapshot: `runtime_audit/{snapshot_dir.name}/SUMMARY.md`",
        "",
        "Current active ROS nodes:",
    ]

    if active_ros_nodes:
        lines.extend(f"- `{node}`" for node in active_ros_nodes)
    else:
        lines.append("- None")

    lines.extend(
        [
            "",
            "Current per-node log files:",
        ]
    )
    if node_log_names:
        lines.extend(f"- `nodes/{name}`" for name in node_log_names)
    else:
        lines.append("- None")

    lines.extend(
        [
            "",
            f"Current container log tail: `docker/{container_log_name}`",
            "",
            "Current recent ERROR lines from container logs:",
        ]
    )
    if launch_errors:
        lines.extend(f"- `{line}`" for line in launch_errors)
    else:
        lines.append("- None")

    write_file(start_here, "\n".join(lines) + "\n")


def build_component_rows(
    rosnode_text: str,
    process_text: str,
    node_log_tree: str,
    binary_checks: dict,
) -> List[str]:
    rows = []
    ros_nodes = set(safe_lines(rosnode_text))

    for component in EXPECTED_COMPONENTS:
        running = component.ros_node in ros_nodes
        process_seen = component.process_pattern in process_text
        node_logs = node_log_matches(component.name, node_log_tree)
        rows.append(
            "| {name} | {running} | {process_seen} | {binary_exists} | {node_logs} |".format(
                name=component.name,
                running=bool_mark(running),
                process_seen=bool_mark(process_seen),
                binary_exists=bool_mark(binary_checks.get(component.binary_path, False)),
                node_logs=len(node_logs),
            )
        )

    return rows


def inside_container_runner(command: str) -> subprocess.CompletedProcess:
    return run_command(["bash", "-lc", command])


def host_runner(container: str) -> Callable[[str], subprocess.CompletedProcess]:
    def _runner(command: str) -> subprocess.CompletedProcess:
        return run_command(["docker", "exec", container, "bash", "-lc", command])

    return _runner


def collect_snapshot(
    mode: str,
    container: str,
    runner: Callable[[str], subprocess.CompletedProcess],
) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    snapshot_dir = AUDIT_ROOT / timestamp
    snapshot_dir.mkdir(parents=True, exist_ok=True)
    DOCKER_LOG_ROOT.mkdir(parents=True, exist_ok=True)

    if mode == "host":
        docker_ps = run_command(["docker", "ps", "-a", "--format", "{{.Names}}\t{{.Status}}\t{{.Image}}"])
        docker_inspect = run_command(["docker", "inspect", container])
        docker_logs = run_command(["docker", "logs", "--timestamps", "--tail", "400", container])
        store_completed(snapshot_dir / "docker_ps.txt", docker_ps)
        store_completed(snapshot_dir / "docker_inspect.json", docker_inspect)
        store_completed(snapshot_dir / "docker_logs_tail.txt", docker_logs)
        write_file(
            DOCKER_LOG_ROOT / f"{container}-container-tail.log",
            docker_logs.stdout + docker_logs.stderr,
        )
    else:
        note = subprocess.CompletedProcess(
            args=["inside-container"],
            returncode=0,
            stdout="inside-container mode: host-level docker metadata not collected\n",
            stderr="",
        )
        store_completed(snapshot_dir / "docker_ps.txt", note)
        store_completed(snapshot_dir / "docker_inspect.json", note)
        store_completed(snapshot_dir / "docker_logs_tail.txt", note)
        docker_logs = note

    container_env = runner("env | sort")
    container_processes = runner("ps -ef")
    container_runtime = runner("pwd; whoami; hostname; hostname -I 2>/dev/null || true")
    store_completed(snapshot_dir / "container_env.txt", container_env)
    store_completed(snapshot_dir / "container_processes.txt", container_processes)
    store_completed(snapshot_dir / "container_runtime.txt", container_runtime)

    rosnode_list = runner(f"{ROS_SHELL_PREFIX}\nrosnode list | sort")
    rostopic_list = runner(f"{ROS_SHELL_PREFIX}\nrostopic list | sort")
    rosparam_list = runner(f"{ROS_SHELL_PREFIX}\nrosparam list | sort")
    rosout_recent = runner(
        f"{ROS_SHELL_PREFIX}\ntimeout 3s rostopic echo -p /rosout_agg | sed -n '1,160p'"
    )
    store_completed(snapshot_dir / "rosnode_list.txt", rosnode_list)
    store_completed(snapshot_dir / "rostopic_list.txt", rostopic_list)
    store_completed(snapshot_dir / "rosparam_list.txt", rosparam_list)
    store_completed(snapshot_dir / "rosout_recent.csv", rosout_recent)

    node_logs_tree = runner("find /home/orangepi/robot/logs/nodes -mindepth 2 -maxdepth 2 -type f | sort 2>/dev/null || true")
    node_logs_tail = runner(
        "find /home/orangepi/robot/logs/nodes -mindepth 2 -maxdepth 2 -type f | sort | while read -r f; do [ -f \"$f\" ] || continue; echo \"===== ${f} =====\"; tail -n 120 \"$f\"; echo; done"
    )
    store_completed(snapshot_dir / "node_logs_tree.txt", node_logs_tree)
    store_completed(snapshot_dir / "node_logs_tail.txt", node_logs_tail)

    binary_checks = {}
    for component in EXPECTED_COMPONENTS:
        result = runner(f"test -x {component.binary_path} && echo present || echo missing")
        binary_checks[component.binary_path] = "present" in result.stdout

    rows = build_component_rows(
        rosnode_list.stdout,
        container_processes.stdout,
        node_logs_tree.stdout,
        binary_checks,
    )

    launch_errors = extract_launch_errors(docker_logs.stdout + docker_logs.stderr)
    active_ros_nodes = relevant_ros_nodes(rosnode_list.stdout)
    collected_node_logs = node_log_paths(node_logs_tree.stdout)
    node_log_names = [node_log_display_name(path) for path in collected_node_logs]
    summary_lines = [
        f"# Runtime Audit for `{container}`",
        "",
        f"- Timestamp (UTC): `{timestamp}`",
        f"- Collection mode: `{mode}`",
        f"- Project root: `{PROJECT_ROOT}`",
        f"- Logs root: `{LOG_ROOT}`",
        "",
        "## Current ROS/Process Summary",
        "",
        f"- ROS nodes discovered: `{len(active_ros_nodes)}`",
        f"- Topics discovered: `{len(safe_lines(rostopic_list.stdout))}`",
        f"- Per-node rosout files: `{len(collected_node_logs)}`",
        f"- Recent container log ERROR lines: `{len(launch_errors)}`",
        "",
        "## Runtime Flags",
        "",
        f"- `ROBOT_START_ROSCORE={env_flag(container_env.stdout, 'ROBOT_START_ROSCORE')}`",
        f"- `ROBOT_START_ROSOUT_SPLITTER={env_flag(container_env.stdout, 'ROBOT_START_ROSOUT_SPLITTER')}`",
        f"- `ROBOT_START_WEB={env_flag(container_env.stdout, 'ROBOT_START_WEB')}`",
        f"- `ROBOT_START_AGENT={env_flag(container_env.stdout, 'ROBOT_START_AGENT')}`",
        f"- `ROBOT_START_BASE_CONTROL={env_flag(container_env.stdout, 'ROBOT_START_BASE_CONTROL')}`",
        f"- `ROBOT_START_ALL_LAUNCH={env_flag(container_env.stdout, 'ROBOT_START_ALL_LAUNCH')}`",
        "",
        "## Active ROS Nodes",
        "",
    ]

    if active_ros_nodes:
        summary_lines.extend(f"- `{node}`" for node in active_ros_nodes)
    else:
        summary_lines.append("- No ROS nodes discovered.")

    summary_lines.extend(
        [
            "",
            "## Collected Per-Node Log Files",
            "",
        ]
    )

    if collected_node_logs:
        summary_lines.extend(f"- `{node_log_display_name(path)}`" for path in collected_node_logs)
    else:
        summary_lines.append("- No per-node rosout log files collected yet.")

    summary_lines.extend(
        [
            "",
            "## Expected Component Status",
            "",
            "| component | rosnode_running | process_seen | binary_present | node_log_files |",
            "| --- | --- | --- | --- | --- |",
            *rows,
            "",
            "## Recent ERROR Lines From Container Logs",
            "",
        ]
    )

    if launch_errors:
        summary_lines.extend(f"- {line}" for line in launch_errors)
    else:
        summary_lines.append("- No `ERROR:` lines found in recent container logs.")

    summary_lines.extend(
        [
            "",
            "## Files in This Snapshot",
            "",
            "- `docker_ps.txt`",
            "- `docker_inspect.json`",
            "- `docker_logs_tail.txt`",
            "- `container_env.txt`",
            "- `container_processes.txt`",
            "- `container_runtime.txt`",
            "- `rosnode_list.txt`",
            "- `rostopic_list.txt`",
            "- `rosparam_list.txt`",
            "- `rosout_recent.csv`",
            "- `node_logs_tree.txt`",
            "- `node_logs_tail.txt`",
        ]
    )

    write_file(snapshot_dir / "SUMMARY.md", "\n".join(summary_lines) + "\n")
    ensure_latest_symlink(snapshot_dir)
    write_logs_index(
        snapshot_dir=snapshot_dir,
        active_ros_nodes=active_ros_nodes,
        node_log_names=node_log_names,
        container_log_name=f"{container}-container-tail.log",
        launch_errors=launch_errors,
    )
    return snapshot_dir


def main() -> int:
    inside_container = "--inside-container" in sys.argv[1:]
    container = DEFAULT_CONTAINER

    positional = [arg for arg in sys.argv[1:] if arg != "--inside-container"]
    if positional:
        container = positional[0]

    if inside_container:
        snapshot_dir = collect_snapshot("inside-container", container, inside_container_runner)
    else:
        snapshot_dir = collect_snapshot("host", container, host_runner(container))

    print(snapshot_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
