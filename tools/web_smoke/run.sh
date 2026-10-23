#!/usr/bin/env bash
# Web export smoke test (see docs/PLAN.md, "Web export spike").
#
# Prerequisites:
#   - project/addons/horde/bin/web/libhorde.web.template_release.wasm32.nothreads.wasm
#     (scons platform=web target=template_release threads=no, Emscripten matching the
#     templates: 4.0.20 for Godot 4.6/4.7, 3.1.64 for 4.3/4.4).
#   - The Godot export templates for <godot> installed (web_dlink_nothreads_release.zip).
#   - Node.js, and a Chromium binary in $CHROMIUM (default: Playwright's under /opt/pw-browsers).
#
#   tools/web_smoke/run.sh <godot executable> [port]
#
# Exports a small scene that uses Horde2D, serves it WITHOUT cross-origin isolation
# headers from 127.0.0.1 and checks that the extension loads and simulates.
set -euo pipefail

godot="${1:?usage: run.sh <godot executable> [port]}"
port="${2:-8060}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
work="$(mktemp -d)"
trap 'kill "${server_pid:-0}" 2>/dev/null || true; rm -rf "${work}"' EXIT

mkdir -p "${work}/proj/addons" "${work}/proj/out"
cp "${here}/project.godot" "${here}/export_presets.cfg" "${here}/web_smoke.gd" "${here}/web_smoke.tscn" "${work}/proj/"
cp -r "${root}/project/addons/horde" "${work}/proj/addons/"
# Only the web library is needed; the editor may print errors about the missing
# desktop library during export, which is harmless here.
find "${work}/proj/addons/horde/bin" -type f ! -name "*.wasm" -delete

"${godot}" --headless --path "${work}/proj" --import >/dev/null 2>&1 || true
"${godot}" --headless --path "${work}/proj" --export-release "Web" "${work}/proj/out/index.html" >/dev/null 2>&1 || true
test -f "${work}/proj/out/index.html" || { echo "Export failed" >&2; exit 1; }

(cd "${here}" && npm install --silent --no-audit --no-fund >/dev/null)
python3 -m http.server "${port}" --bind 127.0.0.1 --directory "${work}/proj/out" >/dev/null 2>&1 &
server_pid=$!
sleep 1

output="$(cd "${here}" && node run.js "http://127.0.0.1:${port}/index.html" 120000)"
echo "${output}"
grep -q '"result": "OK' <<<"${output}"
