#!/usr/bin/env bash
# Runs the headless benchmark and writes a CSV.
#
#   tools/run_bench.sh <godot executable> [output.csv] [extra args...]
#
# Extra args are passed to the bench scene, e.g. --sizes=1000,5000 --impls=horde
set -euo pipefail

godot="${1:?usage: run_bench.sh <godot executable> [output.csv] [extra args...]}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${2:-${root}/project/bench/results/latest.tmp.csv}"
shift $(( $# >= 2 ? 2 : 1 ))
mkdir -p "$(dirname "${out}")"
out="$(cd "$(dirname "${out}")" && pwd)/$(basename "${out}")"

"${godot}" --headless --path "${root}/project" --import >/dev/null 2>&1 || true
"${godot}" --headless --fixed-fps 60 --path "${root}/project" res://bench/bench.tscn -- --out="${out}" "$@"
