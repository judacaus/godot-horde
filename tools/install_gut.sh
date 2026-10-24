#!/usr/bin/env bash
# Installs GUT (Godot Unit Test) into project/addons/gut. GUT releases track Godot
# minor versions, so the GUT tag is picked from the Godot version under test.
#
#   tools/install_gut.sh 4.7.2
set -euo pipefail

godot_version="${1:?usage: install_gut.sh <godot version, e.g. 4.7.2>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "${godot_version}" in
	4.4*) tag="v9.4.0" ;; # GUT 9.4.0 supports Godot 4.3 - 4.4.
	4.5*) tag="v9.5.0" ;;
	4.6*) tag="v9.6.1" ;;
	4.7*) tag="v9.7.1" ;;
	*) echo "No GUT version mapped for Godot ${godot_version}" >&2; exit 1 ;;
esac

target="${root}/project/addons/gut"
stamp="${target}/.horde_gut_tag"
if [[ -f "${stamp}" && "$(cat "${stamp}")" == "${tag}" ]]; then
	echo "GUT ${tag} already installed"
	exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${tag}" https://github.com/bitwes/Gut.git "${tmp}/gut"
rm -rf "${target}"
cp -r "${tmp}/gut/addons/gut" "${target}"
echo "${tag}" > "${stamp}"
echo "Installed GUT ${tag} into project/addons/gut"
