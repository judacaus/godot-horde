#!/usr/bin/env bash
# Downloads an official Godot Linux x86_64 build from GitHub releases into
# .tools/godot-<version>/ and verifies it against the release's SHA512-SUMS.txt.
# Prints the path of the executable on the last line.
#
#   tools/fetch_godot.sh 4.7.2
set -euo pipefail

version="${1:?usage: fetch_godot.sh <version, e.g. 4.7.2>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/.tools/godot-${version}"
name="Godot_v${version}-stable_linux.x86_64"
base="https://github.com/godotengine/godot/releases/download/${version}-stable"

if [[ ! -x "${dest}/${name}" ]]; then
	mkdir -p "${dest}"
	curl -fsSL --retry 4 -o "${dest}/${name}.zip" "${base}/${name}.zip"
	curl -fsSL --retry 4 -o "${dest}/SHA512-SUMS.txt" "${base}/SHA512-SUMS.txt"
	(cd "${dest}" && grep " ${name}.zip\$" SHA512-SUMS.txt | sha512sum -c - >&2)
	unzip -q -o "${dest}/${name}.zip" -d "${dest}"
	rm "${dest}/${name}.zip"
	chmod +x "${dest}/${name}"
fi
echo "${dest}/${name}"
