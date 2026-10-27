#!/usr/bin/env bash
set -euo pipefail

project_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
install_prefix="${BIT8SHELL_INSTALL_PREFIX:-$HOME/.local}"

cd "$project_dir"
if [[ -n "${BIT8SHELL_PUTTYGEN_EXECUTABLE:-}" ]]; then
    converter_path="$BIT8SHELL_PUTTYGEN_EXECUTABLE"
    [[ -x "$converter_path" ]] || { echo "PuTTYgen is not executable: $converter_path" >&2; exit 2; }
else
    converter_path="$project_dir/build/tools/puttygen"
    if [[ ! -x "$converter_path" ]]; then
        mkdir -p "$project_dir/build/tools"
        python3 "$project_dir/scripts/ci/build-puttygen.py" --output "$converter_path"
    fi
fi

cmake --preset release -DBIT8SHELL_PUTTYGEN_EXECUTABLE="$converter_path"
cmake --build --preset release --parallel
ctest --test-dir build/release --output-on-failure
cmake --install build/release --prefix "$install_prefix"

echo "8bitShell was installed under: $install_prefix"
echo "Launch it from the application menu or run: $install_prefix/bin/bitshell"
