#!/usr/bin/env bash
# Runs the headless GUT suite in project/ with the given Godot executable.
# The extension must be built first (scons) and GUT installed (tools/install_gut.sh).
#
#   tools/run_godot_tests.sh .tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64
set -euo pipefail

godot="${1:?usage: run_godot_tests.sh <path to Godot executable>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project="${root}/project"

if [[ ! -d "${project}/addons/gut" ]]; then
	echo "GUT is not installed. Run tools/install_gut.sh <godot version> first." >&2
	exit 1
fi

# Import once so the global class cache (GutTest, ...) and the extension list exist.
"${godot}" --headless --path "${project}" --import >/dev/null 2>&1 || true

log="$(mktemp)"
trap 'rm -f "${log}"' EXIT
set +e
"${godot}" --headless --path "${project}" -s res://addons/gut/gut_cmdln.gd -gconfig=res://.gutconfig.json -gexit 2>&1 | tee "${log}"
status=${PIPESTATUS[0]}
set -e

# Older GUT versions may exit 0 on failures; double-check the summary.
if grep -qE "^(\s*)(Failing tests|\[FAILED\])|---- [0-9]+ failing tests ----" "${log}"; then
	status=1
fi
if ! grep -qE "All tests passed|Passing Tests|Tests +[0-9]+" "${log}"; then
	echo "Could not find a GUT summary in the output." >&2
	status=1
fi
exit "${status}"
