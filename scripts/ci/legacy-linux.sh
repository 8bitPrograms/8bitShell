#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends python3 python3-pip g++-12 ninja-build cmake git pkg-config perl patchelf rpm zstd openssh-client openssh-server putty-tools libssl-dev libgl1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev libxkbcommon-dev libxkbcommon-x11-dev libxcb1-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-render-util0-dev libxcb-xinerama0-dev libxcb-xkb-dev libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev libx11-xcb-dev libxrender-dev libxi-dev libxext-dev libxfixes-dev libwayland-dev libwayland-egl-backend-dev wayland-protocols libdbus-1-dev fonts-dejavu-core
python3 scripts/ci/enable-source-repos.py
apt-get update
python3 -m pip install --break-system-packages cmake==3.31.6
mkdir -p /run/sshd
useradd -m -u 1000 builder
chown -R builder:builder /src
export HOME=/home/builder
su -m builder -s /bin/bash -c 'python3 scripts/ci/build-qt.py --prefix "tooling/qt-sdk-$RELEASE_ARCH" --jobs 2 && python3 scripts/ci/build.py --qt-root "tooling/qt-sdk-$RELEASE_ARCH" --jobs 2'
