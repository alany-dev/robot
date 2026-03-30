#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="${ROOT_DIR}/docker/build/vendor/rk"
LIB_DIR="${VENDOR_DIR}/lib"
MANIFEST_FILE="${ROOT_DIR}/docker/build/vendor/rk_manifest.txt"

RKNN_API_LIB="${RKNN_API_LIB:-/usr/lib/librknn_api.so}"
RKNN_RT_LIB="${RKNN_RT_LIB:-}"

if [[ -z "${RKNN_RT_LIB}" ]]; then
  if [[ -f /usr/lib/librknnrt.so ]]; then
    RKNN_RT_LIB="/usr/lib/librknnrt.so"
  elif [[ -f /lib/librknnrt.so ]]; then
    RKNN_RT_LIB="/lib/librknnrt.so"
  else
    RKNN_RT_LIB="/usr/lib/librknnrt.so"
  fi
fi

missing=()
abi_errors=()

require_path() {
  local path="$1"
  if [[ ! -e "${path}" ]]; then
    missing+=("${path}")
  fi
}

copy_glob() {
  local pattern="$1"
  local target_dir="$2"
  local matches=()

  while IFS= read -r match; do
    matches+=("${match}")
  done < <(compgen -G "${pattern}" || true)

  if [[ "${#matches[@]}" -eq 0 ]]; then
    missing+=("${pattern}")
    return
  fi

  cp -a "${matches[@]}" "${target_dir}/"
}

check_abi_compat() {
  local path="$1"
  local label="$2"
  local matches

  if [[ ! -f "${path}" ]]; then
    return
  fi

  if ! command -v objdump >/dev/null 2>&1; then
    return
  fi

  matches="$(
    objdump -T "${path}" 2>/dev/null \
      | grep -E 'GLIBC_2\.(3[2-9]|[4-9][0-9])|GLIBCXX_3\.4\.(29|[3-9][0-9])' \
      | head -n 12 \
      || true
  )"

  if [[ -n "${matches}" ]]; then
    abi_errors+=("${label}: ${path}")
    abi_errors+=("${matches}")
  fi
}

require_path "${RKNN_API_LIB}"
require_path "${RKNN_RT_LIB}"

if [[ "${#missing[@]}" -gt 0 ]]; then
  cat >&2 <<EOF
缺少镜像构建所需的 RKNN runtime 产物，prepare 失败。

缺失路径:
$(printf '  - %s\n' "${missing[@]}")

至少需要以下安装产物存在:
  - ${RKNN_API_LIB}
  - ${RKNN_RT_LIB}

说明:
  - rkmpp / rkrga 已改为容器启动阶段从 /home/orangepi/soft 内部源码编译安装
  - 这里仅保留无法从本仓库源码直接重编的 RKNN runtime 二进制打包逻辑
EOF
  exit 1
fi

check_abi_compat "${RKNN_API_LIB}" "rknn_api"
check_abi_compat "${RKNN_RT_LIB}" "rknnrt"

if [[ "${#abi_errors[@]}" -gt 0 ]]; then
  cat >&2 <<EOF
检测到 RKNN runtime ABI 高于当前 Docker 基底 ubuntu:20.04 可兼容范围，prepare 失败。

以下库引用了更高版本的 glibc / libstdc++ 符号：
$(printf '  %s\n' "${abi_errors[@]}")

这部分库仍然以宿主机二进制形式进入镜像，因此它们也必须与当前镜像基底兼容。
EOF
  exit 1
fi

rm -rf "${VENDOR_DIR}"
mkdir -p "${LIB_DIR}"

copy_glob "${RKNN_API_LIB}" "${LIB_DIR}"
copy_glob "${RKNN_RT_LIB}" "${LIB_DIR}"

cat > "${MANIFEST_FILE}" <<EOF
generated_at=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
rknn_api_lib=${RKNN_API_LIB}
rknn_rt_lib=${RKNN_RT_LIB}
note=rkmpp_and_rkrga_are_built_inside_container_from_/home/orangepi/soft
EOF

echo "prepared RKNN vendor payload in ${VENDOR_DIR}"
