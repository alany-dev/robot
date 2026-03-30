#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="${ROBOT_WORKSPACE:-/home/orangepi/robot}"
NODE_LOG_DIR="${ROBOT_NODE_LOG_DIR:-${WORKSPACE}/logs/nodes}"
NODE_LOG_MAX_BYTES="${ROBOT_NODE_LOG_MAX_BYTES:-20971520}"
LOG_RETENTION_SECONDS="${ROBOT_LOG_RETENTION_SECONDS:-3600}"

declare -a pids=()
declare -a critical_pids=()
declare -A pid_names=()
declare -A pid_critical=()

log() {
  echo "[robot-entrypoint] $*"
}

safe_source() {
  local had_nounset=0

  if [[ $- == *u* ]]; then
    had_nounset=1
    set +u
  fi

  # shellcheck disable=SC1090
  source "$1"

  if [[ "${had_nounset}" == "1" ]]; then
    set -u
  fi
}

first_ipv4() {
  hostname -I | tr ' ' '\n' | awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ {print; exit}'
}

prepare_ros_env() {
  local ip
  ip="$(first_ipv4 || true)"
  if [[ -z "${ip}" ]]; then
    ip="127.0.0.1"
  fi

  export ROS_IP="${ROS_IP:-${ip}}"
  export ROS_HOSTNAME="${ROS_HOSTNAME:-${ROS_IP}}"
  export ROS_MASTER_URI="${ROS_MASTER_URI:-http://${ROS_IP}:11311}"
}

prepare_audio() {
  if command -v pactl >/dev/null 2>&1; then
    pactl set-default-sink "alsa_output.platform-rk809-sound.stereo-fallback" >/dev/null 2>&1 || true
    pactl set-default-source "alsa_input.usb-C-Media_Electronics_Inc._USB_PnP_Sound_Device-00.mono-fallback" >/dev/null 2>&1 || true
  fi

  if command -v amixer >/dev/null 2>&1; then
    amixer -c 2 sset Mic 16 >/dev/null 2>&1 || true
  fi
}

ensure_workspace() {
  if [[ ! -d "${WORKSPACE}/src" ]]; then
    log "workspace not found: ${WORKSPACE}"
    exit 1
  fi
}

maybe_prepare_rk_userlibs() {
  if [[ "${ROBOT_AUTO_BUILD_RK_USERLIBS:-1}" != "1" ]]; then
    log "skip RK userlib build because ROBOT_AUTO_BUILD_RK_USERLIBS=${ROBOT_AUTO_BUILD_RK_USERLIBS:-0}"
    return
  fi

  log "checking container RK userlibs"
  bash "${WORKSPACE}/docker/build/runtime/build_rk_userlibs.sh"
}

workspace_needs_build() {
  if [[ ! -f "${WORKSPACE}/devel/setup.bash" ]]; then
    return 0
  fi

  if [[ "${ROBOT_START_BASE_CONTROL:-1}" == "1" && ! -x "${WORKSPACE}/devel/lib/base_control/base_control" ]]; then
    log "missing base_control executable in devel space, rebuilding workspace"
    return 0
  fi

  if [[ "${ROBOT_START_ALL_LAUNCH:-0}" == "1" ]]; then
    local required_bins=(
      "${WORKSPACE}/devel/lib/usb_camera/usb_camera"
      "${WORKSPACE}/devel/lib/img_decode/img_decode"
      "${WORKSPACE}/devel/lib/img_encode/img_encode"
      "${WORKSPACE}/devel/lib/rknn_yolov6/rknn_yolov6"
      "${WORKSPACE}/devel/lib/ydlidar/ydlidar_node"
      "${WORKSPACE}/devel/lib/object_track/object_track"
    )
    local bin_path
    for bin_path in "${required_bins[@]}"; do
      if [[ ! -x "${bin_path}" ]]; then
        log "missing launch executable in devel space: ${bin_path}"
        return 0
      fi
    done
  fi

  return 1
}

maybe_catkin_make() {
  safe_source /opt/ros/noetic/setup.bash

  if [[ "${ROBOT_AUTO_CATKIN_MAKE:-1}" != "1" ]]; then
    log "skip catkin_make because ROBOT_AUTO_CATKIN_MAKE=${ROBOT_AUTO_CATKIN_MAKE:-0}"
    return
  fi

  if [[ "${ROBOT_FORCE_CATKIN_MAKE:-0}" == "1" ]] || workspace_needs_build; then
    log "running catkin_make in ${WORKSPACE}"
    (
      cd "${WORKSPACE}"
      catkin_make
    )
    return
  fi

  log "reuse existing devel/setup.bash, set ROBOT_FORCE_CATKIN_MAKE=1 to rebuild"
}

source_workspace() {
  safe_source /opt/ros/noetic/setup.bash

  if [[ -f "${WORKSPACE}/devel/setup.bash" ]]; then
    safe_source "${WORKSPACE}/devel/setup.bash"
  fi

  export ROBOT_NODE_LOG_DIR="${NODE_LOG_DIR}"
  export ROBOT_NODE_LOG_MAX_BYTES="${NODE_LOG_MAX_BYTES}"
  export ROBOT_LOG_RETENTION_SECONDS="${LOG_RETENTION_SECONDS}"
  export PYTHONPATH="${WORKSPACE}/src/agent:${PYTHONPATH:-}"
}

start_process() {
  local name="$1"
  local critical="${2:-1}"
  shift 2

  log "starting ${name}"
  "$@" &
  local pid=$!
  pids+=("${pid}")
  if [[ "${critical}" == "1" ]]; then
    critical_pids+=("${pid}")
  fi
  pid_names["${pid}"]="${name}"
  pid_critical["${pid}"]="${critical}"
}

remove_pid() {
  local target="$1"
  local pid
  local -a next_pids=()
  local -a next_critical_pids=()

  for pid in "${pids[@]}"; do
    if [[ "${pid}" != "${target}" ]]; then
      next_pids+=("${pid}")
    fi
  done
  pids=("${next_pids[@]}")

  for pid in "${critical_pids[@]}"; do
    if [[ "${pid}" != "${target}" ]]; then
      next_critical_pids+=("${pid}")
    fi
  done
  critical_pids=("${next_critical_pids[@]}")

  unset 'pid_names[$target]'
  unset 'pid_critical[$target]'
}

find_exited_pid() {
  local pid
  for pid in "${pids[@]}"; do
    if ! kill -0 "${pid}" >/dev/null 2>&1; then
      printf '%s\n' "${pid}"
      return 0
    fi
  done

  return 1
}

stop_children() {
  local pid
  for pid in "${pids[@]}"; do
    if kill -0 "${pid}" >/dev/null 2>&1; then
      kill "${pid}" >/dev/null 2>&1 || true
    fi
  done

  for pid in "${pids[@]}"; do
    wait "${pid}" >/dev/null 2>&1 || true
  done
}

on_exit() {
  local exit_code=$?
  trap - EXIT INT TERM
  stop_children
  exit "${exit_code}"
}

trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

main() {
  mkdir -p "${NODE_LOG_DIR}"
  ensure_workspace
  prepare_ros_env

  log "ROS_MASTER_URI=${ROS_MASTER_URI}"
  log "ROS_IP=${ROS_IP}"
  log "node logs: ${NODE_LOG_DIR}"
  log "node log max bytes: ${NODE_LOG_MAX_BYTES}"
  log "log retention seconds: ${LOG_RETENTION_SECONDS}"
  log "container stdout/stderr: docker logs ${ROBOT_CONTAINER_NAME:-robot}"

  if [[ "${ROBOT_AP_MODE:-0}" == "1" ]]; then
    export ROBOT_NODE_LOG_DIR="${NODE_LOG_DIR}"
    export ROBOT_NODE_LOG_MAX_BYTES="${NODE_LOG_MAX_BYTES}"
    export ROBOT_LOG_RETENTION_SECONDS="${LOG_RETENTION_SECONDS}"
    safe_source /opt/ros/noetic/setup.bash
    exec python3 "${WORKSPACE}/web_controller.py" --ap-mode
  fi

  maybe_prepare_rk_userlibs
  maybe_catkin_make
  source_workspace
  prepare_audio

  if [[ "${ROBOT_START_ROSCORE:-1}" == "1" ]]; then
    start_process "roscore" 1 roscore
    sleep 3
  fi

  if [[ "${ROBOT_START_ROSOUT_SPLITTER:-1}" == "1" ]]; then
    start_process "rosout_splitter" 0 python3 "${WORKSPACE}/docker/build/runtime/rosout_splitter.py"
    sleep 1
  fi

  if [[ "${ROBOT_START_WEB:-1}" == "1" ]]; then
    start_process "web_controller" 1 python3 "${WORKSPACE}/web_controller.py"
  fi

  if [[ "${ROBOT_START_AGENT:-1}" == "1" ]]; then
    start_process "agent" "${ROBOT_AGENT_REQUIRED:-0}" nice -n 0 "${WORKSPACE}/src/agent/start_agent.sh"
  fi

  if [[ "${ROBOT_START_BASE_CONTROL:-1}" == "1" ]]; then
    start_process "base_control" 1 taskset -c 1,2 nice -n 15 roslaunch base_control base_control.launch
  fi

  if [[ "${ROBOT_START_ALL_LAUNCH:-0}" == "1" ]]; then
    start_process "all_launch" 1 roslaunch pkg_launch all.launch
  fi

  if [[ "${#pids[@]}" -eq 0 ]]; then
    log "nothing to run, dropping into bash"
    exec /bin/bash
  fi

  local finished_pid status name critical
  while [[ "${#pids[@]}" -gt 0 ]]; do
    if wait -n "${pids[@]}"; then
      status=0
    else
      status=$?
    fi

    finished_pid="$(find_exited_pid || true)"
    if [[ -z "${finished_pid}" ]]; then
      continue
    fi

    name="${pid_names[${finished_pid}]:-unknown}"
    critical="${pid_critical[${finished_pid}]:-1}"
    remove_pid "${finished_pid}"

    if [[ "${critical}" == "1" ]]; then
      log "${name} exited, shutting down remaining processes"
      exit "${status}"
    fi

    log "${name} exited with status ${status}, continuing because process is optional"
  done

  log "all processes exited"
}

main "$@"
