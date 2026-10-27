#!/usr/bin/env bash
# Read-only local evidence. No config, log, key, agent, or network inspection.
# This is a direct-dependency inventory, NOT a complete SBOM or CVE scanner.
set -euo pipefail
export LC_ALL=C
project_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
binary_path="${1:-build/release/bitshell}"
if [[ ! -f "$binary_path" || -L "$binary_path" ]]; then
    echo "Choose the regular release binary built from this checkout." >&2
    exit 2
fi
for dependency_tool in readelf sha256sum rg pkg-config; do
    command -v "$dependency_tool" >/dev/null || { echo "Missing evidence tool: $dependency_tool" >&2; exit 2; }
done

echo '8bitShell security evidence v1 (not a certification)'
date -u '+Collected: %Y-%m-%dT%H:%M:%SZ'
sha256sum -- "$binary_path"
elf_header="$(readelf -hW -- "$binary_path")"
elf_program="$(readelf -lW -- "$binary_path")"
elf_dynamic="$(readelf -dW -- "$binary_path")"
elf_symbols="$(readelf -sW -- "$binary_path")"
failed_controls=0
check_control() {
    if [[ "$2" == yes ]]; then echo "PASS $1"; else echo "FAIL $1"; failed_controls=$((failed_controls + 1)); fi
}
has_pattern() { if rg -q -- "$1" <<< "$2"; then echo yes; else echo no; fi; }
check_control 'Position-independent executable (PIE)' "$(has_pattern 'Type:.*DYN' "$elf_header")"
check_control 'RELRO segment' "$(has_pattern 'GNU_RELRO' "$elf_program")"
check_control 'Immediate symbol binding (full RELRO)' "$(has_pattern 'BIND_NOW|FLAGS.*NOW' "$elf_dynamic")"
check_control 'Non-executable stack' "$(has_pattern 'GNU_STACK.* RW ' "$elf_program")"
check_control 'Stack canary reference' "$(has_pattern '__stack_chk_fail' "$elf_symbols")"
check_control 'Fortified libc reference (release)' "$(has_pattern '__[a-zA-Z0-9_]+_chk@' "$elf_symbols")"
runtime_search_ok=yes
instrumentation_ok=yes
if rg -q 'RPATH|RUNPATH' <<< "$elf_dynamic"; then runtime_search_ok=no; fi
if rg -q 'libasan|libubsan' <<< "$elf_dynamic"; then instrumentation_ok=no; fi
check_control 'No embedded runtime search path (native Linux release)' "$runtime_search_ok"
check_control 'No sanitizer runtime in release artifact' "$instrumentation_ok"
converter_path="$(dirname -- "$binary_path")/puttygen"
if [[ ! -f "$converter_path" || -L "$converter_path" || ! -x "$converter_path" ]]; then
    echo 'Missing regular bundled PuTTYgen converter beside the release binary.' >&2
    exit 1
fi
locked_putty_version="$(rg -o '"version": "[0-9.]+"' packaging/puttygen.lock.json | rg -o '[0-9.]+' | head -n1)"
putty_version="$($converter_path --version)"
check_control 'Bundled PuTTYgen matches locked release' \
    "$(has_pattern "^puttygen: Release ${locked_putty_version}$" "$putty_version")"
putty_dynamic="$(readelf -dW -- "$converter_path")"
check_control 'Command-line PuTTYgen has no GUI toolkit dependency' \
    "$(if rg -q 'libgtk|libgdk|libX11' <<< "$putty_dynamic"; then echo no; else echo yes; fi)"
echo 'Direct ELF requirements (no dependency code is executed):'
rg '\(NEEDED\)' <<< "$elf_dynamic"
echo 'Bundled PPK converter and supply-chain lock:'
printf '%s\n' "$putty_version"
sha256sum -- "$converter_path" packaging/puttygen.lock.json third_party/PUTTY-LICENSE
echo 'Installed transport and build dependency versions:'
/usr/bin/ssh -V 2>&1
/usr/bin/openssl version
pkg-config --modversion Qt6Core Qt6Quick
if command -v rpm >/dev/null; then
    rpm -q --qf '%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}\n' openssh-clients qt6-qtbase qt6-qtdeclarative || true
fi
echo 'Bundled terminal parser source and local defensive patch:'
rg 'URL |URL_HASH' cmake/TerminalDependency.cmake
sha256sum cmake/LibvtermPatch.cmake
echo 'YAML source selection and integrity pin:'
rg 'find_package\(yaml-cpp|codeload.github.com/jbeder|URL_HASH' CMakeLists.txt
echo 'Reviewed source manifest (excludes user data and generated build files):'
sha256sum CMakeLists.txt CMakePresets.json third_party/yaml-cpp-LICENSE third_party/PUTTY-LICENSE packaging/puttygen.lock.json
rg --files -0 src qml tests cmake schemas scripts | sort -z | xargs -0 sha256sum
if (( failed_controls > 0 )); then
    echo "$failed_controls release hardening checks failed." >&2
    exit 1
fi
