#!/usr/bin/env bash
set -euo pipefail

TARGET_ROOT="/mnt/nvme/docker"
KEEP_SOURCE=0
DAEMON_JSON="/etc/docker/daemon.json"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_PATH="${DAEMON_JSON}.bak-${TIMESTAMP}"
TMP_DAEMON_JSON="$(mktemp)"
CURRENT_ROOT=""
HAD_DAEMON_JSON=0

usage() {
  cat <<'EOF'
Usage: move_docker_data_root.sh [--target PATH] [--keep-source]

Moves Docker's data-root to a new path, updates /etc/docker/daemon.json,
restarts Docker, and verifies the new root. By default it removes the old
Docker data directory after a successful migration to free root filesystem
space.

Options:
  --target PATH   Destination Docker data-root. Default: /mnt/nvme/docker
  --keep-source   Keep the old Docker data directory after migration
  -h, --help      Show this help message
EOF
}

log() {
  printf '[docker-data-root] %s\n' "$*"
}

run_root() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    log "missing required command: $1"
    exit 1
  fi
}

normalize_path() {
  python3 - "$1" <<'PY'
import os
import sys

print(os.path.abspath(sys.argv[1]))
PY
}

docker_root_dir() {
  docker info --format '{{.DockerRootDir}}' 2>/dev/null || true
}

wait_for_docker_root() {
  local attempts=0
  local root_dir=""

  while [[ "${attempts}" -lt 20 ]]; do
    root_dir="$(docker_root_dir)"
    if [[ -n "${root_dir}" ]]; then
      printf '%s\n' "${root_dir}"
      return 0
    fi
    attempts=$((attempts + 1))
    sleep 1
  done

  return 1
}

write_daemon_json() {
  python3 - "${DAEMON_JSON}" "${TMP_DAEMON_JSON}" "${TARGET_ROOT}" <<'PY'
import json
import pathlib
import sys

daemon_path = pathlib.Path(sys.argv[1])
output_path = pathlib.Path(sys.argv[2])
target_root = sys.argv[3]

data = {}
if daemon_path.exists():
    content = daemon_path.read_text(encoding="utf-8").strip()
    if content:
        data = json.loads(content)

data["data-root"] = target_root
output_path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY

  run_root install -m 0644 "${TMP_DAEMON_JSON}" "${DAEMON_JSON}"
}

restore_daemon_json() {
  if [[ "${HAD_DAEMON_JSON}" == "1" ]]; then
    log "restoring ${DAEMON_JSON} from ${BACKUP_PATH}"
    run_root cp "${BACKUP_PATH}" "${DAEMON_JSON}"
  else
    log "removing generated ${DAEMON_JSON}"
    run_root rm -f "${DAEMON_JSON}"
  fi
}

sync_docker_data() {
  local -a rsync_args=(
    -aHAX
    --numeric-ids
  )

  if [[ ! -d "${CURRENT_ROOT}" ]]; then
    log "source docker root does not exist, skipping data copy: ${CURRENT_ROOT}"
    return
  fi

  log "syncing Docker data from ${CURRENT_ROOT} to ${TARGET_ROOT}"
  run_root mkdir -p "${TARGET_ROOT}"
  run_root chown root:root "${TARGET_ROOT}"

  if command -v rsync >/dev/null 2>&1; then
    if [[ -t 1 ]]; then
      rsync_args+=(--info=progress2)
    fi
    run_root rsync "${rsync_args[@]}" "${CURRENT_ROOT}/" "${TARGET_ROOT}/"
    return
  fi

  log "rsync not found, falling back to cp -a"
  run_root cp -a "${CURRENT_ROOT}/." "${TARGET_ROOT}/"
}

parse_args() {
  while [[ "$#" -gt 0 ]]; do
    case "$1" in
      --target)
        if [[ "$#" -lt 2 ]]; then
          log "--target requires a path"
          exit 1
        fi
        TARGET_ROOT="$2"
        shift 2
        ;;
      --keep-source)
        KEEP_SOURCE=1
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        log "unknown argument: $1"
        usage
        exit 1
        ;;
    esac
  done
}

cleanup() {
  rm -f "${TMP_DAEMON_JSON}"
}

main() {
  trap cleanup EXIT
  parse_args "$@"

  require_cmd docker
  require_cmd install
  require_cmd python3
  require_cmd systemctl

  TARGET_ROOT="$(normalize_path "${TARGET_ROOT}")"
  CURRENT_ROOT="$(docker_root_dir)"
  CURRENT_ROOT="${CURRENT_ROOT:-/var/lib/docker}"

  log "current Docker root: ${CURRENT_ROOT}"
  log "target Docker root: ${TARGET_ROOT}"

  if [[ "${CURRENT_ROOT}" == "${TARGET_ROOT}" ]]; then
    log "Docker root already points to ${TARGET_ROOT}"
    exit 0
  fi

  if run_root test -f "${DAEMON_JSON}"; then
    HAD_DAEMON_JSON=1
    log "backing up ${DAEMON_JSON} to ${BACKUP_PATH}"
    run_root cp "${DAEMON_JSON}" "${BACKUP_PATH}"
  fi

  log "stopping docker.service and docker.socket"
  run_root systemctl stop docker.service
  run_root systemctl stop docker.socket

  sync_docker_data
  write_daemon_json

  log "starting Docker with data-root ${TARGET_ROOT}"
  if ! run_root systemctl restart docker; then
    log "docker restart failed after updating daemon.json"
    restore_daemon_json
    run_root systemctl restart docker || true
    exit 1
  fi

  local verified_root=""
  if ! verified_root="$(wait_for_docker_root)"; then
    log "docker did not come back after restart"
    restore_daemon_json
    run_root systemctl restart docker || true
    exit 1
  fi

  if [[ "${verified_root}" != "${TARGET_ROOT}" ]]; then
    log "docker root verification failed: expected ${TARGET_ROOT}, got ${verified_root}"
    restore_daemon_json
    run_root systemctl restart docker || true
    exit 1
  fi

  log "Docker root verified at ${verified_root}"

  if [[ "${KEEP_SOURCE}" == "0" && -d "${CURRENT_ROOT}" ]]; then
    log "removing old Docker data from ${CURRENT_ROOT} to free root filesystem space"
    run_root rm -rf "${CURRENT_ROOT}"
  else
    log "keeping old Docker data at ${CURRENT_ROOT}"
  fi

  log "migration complete"
}

main "$@"
