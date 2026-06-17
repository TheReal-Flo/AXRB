#!/usr/bin/env bash
set -euo pipefail

name="${1:-AXRB_WSL_D3D12_TestTexture}"
hold_seconds="${2:-60}"
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
probe_dir="${root_dir}/build-tools/d3d12_shared_probe"

mkdir -p "${probe_dir}"
rm -f "${probe_dir}/wsl_producer.out.log" \
      "${probe_dir}/wsl_producer.err.log" \
      "${probe_dir}/wsl_producer.pid"

nohup env LD_LIBRARY_PATH=/usr/lib/wsl/lib \
    "${probe_dir}/wsl_producer" "${name}" "${hold_seconds}" \
    >"${probe_dir}/wsl_producer.out.log" \
    2>"${probe_dir}/wsl_producer.err.log" &

echo "$!" > "${probe_dir}/wsl_producer.pid"
