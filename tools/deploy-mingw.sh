#!/usr/bin/env bash
# Assembles a self-contained Windows deployment folder from a MinGW cross build.
#
#   tools/deploy-mingw.sh <build-dir> <qt-mingw-prefix> <output-dir>
#   e.g. tools/deploy-mingw.sh build-win /opt/qt-win/5.15.2/mingw81_64 deploy/AudioBridge
#
# Copies AudioBridge.exe, AudioBridgeCLI.exe, the Qt DLLs it needs, the Windows
# platform plugin and the MinGW runtime DLLs used by Qt.
set -euo pipefail
BUILD=${1:?build dir}
QT=${2:?qt prefix}
OUT=${3:?output dir}

rm -rf "$OUT"
mkdir -p "$OUT/platforms" "$OUT/styles"
cp "$BUILD/AudioBridge.exe" "$OUT/"
[ -f "$BUILD/AudioBridgeCLI.exe" ] && cp "$BUILD/AudioBridgeCLI.exe" "$OUT/"
for dll in Qt5Core Qt5Gui Qt5Widgets; do
    cp "$QT/bin/$dll.dll" "$OUT/"
done
# Runtime of the compiler Qt was built with (AudioBridge itself links its runtime statically).
for dll in libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do
    cp "$QT/bin/$dll.dll" "$OUT/"
done
cp "$QT/plugins/platforms/qwindows.dll" "$OUT/platforms/"
cp "$QT/plugins/styles/qwindowsvistastyle.dll" "$OUT/styles/"
cp "$(dirname "$0")/../docs/THIRD_PARTY_NOTICES.md" "$OUT/THIRD_PARTY_NOTICES.txt"
echo "Deployed to $OUT:"
find "$OUT" -type f | sort
