#!/usr/bin/env bash
set -euo pipefail

SOFT_DIR="${ROBOT_RK_SOFT_DIR:-/opt/rk-soft}"
RKMPP_SRC="${ROBOT_RKMPP_SOURCE_DIR:-${ROBOT_RKMMP_SOURCE_DIR:-${SOFT_DIR}/rkmpp}}"
RKRGA_SRC="${ROBOT_RKRGA_SOURCE_DIR:-${SOFT_DIR}/rkrga}"
BUILD_ROOT="${ROBOT_RK_BUILD_ROOT:-/tmp/robot-rk-build}"
INSTALL_PREFIX="${ROBOT_RK_INSTALL_PREFIX:-/usr/local}"
MARKER_FILE="${ROBOT_RK_MARKER_FILE:-${INSTALL_PREFIX}/share/robot/rk_userlibs_build.env}"
FORCE_BUILD="${ROBOT_FORCE_BUILD_RK_USERLIBS:-0}"
BUILD_JOBS="${ROBOT_RK_BUILD_JOBS:-$(nproc)}"

ABI_REGEX='GLIBC_2\.(3[2-9]|[4-9][0-9])|GLIBCXX_3\.4\.(29|[3-9][0-9])'

log() {
  echo "[rk-userlibs] $*"
}

source_signature() {
  local src="$1"
  if [[ -d "${src}/.git" ]] && command -v git >/dev/null 2>&1; then
    git -C "${src}" rev-parse HEAD 2>/dev/null || echo "git-unknown"
    return
  fi

  stat -c '%Y' "${src}" 2>/dev/null || echo "mtime-unknown"
}

is_incompatible_abi() {
  local lib_path="$1"
  if [[ ! -f "${lib_path}" ]]; then
    return 0
  fi

  if ! command -v objdump >/dev/null 2>&1; then
    return 1
  fi

  objdump -T "${lib_path}" 2>/dev/null | grep -Eq "${ABI_REGEX}"
}

needs_rebuild() {
  local current_rkmpp_sig current_rkrga_sig
  current_rkmpp_sig="$(source_signature "${RKMPP_SRC}")"
  current_rkrga_sig="$(source_signature "${RKRGA_SRC}")"

  if [[ "${FORCE_BUILD}" == "1" ]]; then
    log "force rebuild enabled"
    return 0
  fi

  if [[ ! -f "${INSTALL_PREFIX}/lib/librockchip_mpp.so" || ! -f "${INSTALL_PREFIX}/lib/librga.so" ]]; then
    log "missing installed RK userlibs"
    return 0
  fi

  if [[ ! -e "/usr/include/rockchip" || ! -e "/usr/include/rga" ]]; then
    log "missing include links for rockchip/rga headers"
    return 0
  fi

  if is_incompatible_abi "${INSTALL_PREFIX}/lib/librockchip_mpp.so"; then
    log "installed librockchip_mpp.so is ABI-incompatible with current container"
    return 0
  fi

  if is_incompatible_abi "${INSTALL_PREFIX}/lib/librga.so"; then
    log "installed librga.so is ABI-incompatible with current container"
    return 0
  fi

  if [[ ! -f "${MARKER_FILE}" ]]; then
    log "missing RK build marker"
    return 0
  fi

  # shellcheck disable=SC1090
  source "${MARKER_FILE}"

  if [[ "${marker_rkmpp_sig:-}" != "${current_rkmpp_sig}" || "${marker_rkrga_sig:-}" != "${current_rkrga_sig}" ]]; then
    log "source signature changed since last RK build"
    return 0
  fi

  return 1
}

require_dir() {
  local dir="$1"
  if [[ ! -d "${dir}" ]]; then
    echo "missing directory: ${dir}" >&2
    exit 1
  fi
}

stage_source_tree() {
  local name="$1"
  local src="$2"
  local staged_src="${BUILD_ROOT}/sources/${name}"

  rm -rf "${staged_src}"
  mkdir -p "${staged_src}"

  (
    cd "${src}"
    tar \
      --exclude=.git \
      --exclude=install \
      --exclude=rkrga_build \
      -cf - .
  ) | (
    cd "${staged_src}"
    tar -xf -
  )

  printf '%s\n' "${staged_src}"
}

build_rkmpp() {
  local staged_src
  staged_src="$(stage_source_tree "rkmpp" "${RKMPP_SRC}")"

  log "building rkmpp from ${RKMPP_SRC}"
  rm -rf "${BUILD_ROOT}/rkmpp"
  cmake -S "${staged_src}" -B "${BUILD_ROOT}/rkmpp" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DBUILD_TEST=OFF
  cmake --build "${BUILD_ROOT}/rkmpp" -j"${BUILD_JOBS}"
  cmake --install "${BUILD_ROOT}/rkmpp"
}

build_rkrga() {
  local staged_src
  staged_src="$(stage_source_tree "rkrga" "${RKRGA_SRC}")"

  log "building rkrga from ${RKRGA_SRC}"
  rm -rf "${BUILD_ROOT}/rkrga"
  cmake -S "${staged_src}" -B "${BUILD_ROOT}/rkrga" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_BUILD_TARGET=cmake_linux \
    -DRGA_SAMPLES_ENABLE=OFF
  cmake --build "${BUILD_ROOT}/rkrga" -j"${BUILD_JOBS}"
  cmake --install "${BUILD_ROOT}/rkrga"
}

install_header_links() {
  mkdir -p /usr/include
  rm -rf /usr/include/rockchip /usr/include/rga
  ln -s "${INSTALL_PREFIX}/include/rockchip" /usr/include/rockchip
  ln -s "${INSTALL_PREFIX}/include/rga" /usr/include/rga
}

write_marker() {
  mkdir -p "$(dirname "${MARKER_FILE}")"
  cat > "${MARKER_FILE}" <<EOF
marker_generated_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
marker_rkmpp_sig=$(source_signature "${RKMPP_SRC}")
marker_rkrga_sig=$(source_signature "${RKRGA_SRC}")
marker_install_prefix=${INSTALL_PREFIX}
EOF
}

main() {
  require_dir "${RKMPP_SRC}"
  require_dir "${RKRGA_SRC}"

  if needs_rebuild; then
    mkdir -p "${BUILD_ROOT}"
    build_rkmpp
    build_rkrga
    install_header_links
    ldconfig

    if is_incompatible_abi "${INSTALL_PREFIX}/lib/librockchip_mpp.so" || is_incompatible_abi "${INSTALL_PREFIX}/lib/librga.so"; then
      echo "rebuilt RK userlibs are still ABI-incompatible with this container" >&2
      exit 1
    fi

    write_marker
    log "RK userlibs rebuilt successfully"
    return 0
  fi

  log "reuse existing container-built RK userlibs"
}

main "$@"
