# Building AudioBridge

## Dependencies

| Dependency | Version | Used for |
|---|---|---|
| C++17 compiler | MSVC 2019/2022, or MinGW-w64 GCC ≥ 10 | everything |
| CMake | ≥ 3.16 | build system |
| Qt | 5.15.x (Widgets only) | desktop application |
| Windows SDK | 10.0.17763 or newer | WASAPI / Core Audio headers (`mmdeviceapi.h`, `audioclient.h`, `devicetopology.h`), `avrt`, `ole32` |
| Inno Setup | 6.1 or newer | installer only |
| Ninja | optional | faster builds |

The audio engine (`src/core`) has **no** external dependencies. Qt is used only for the GUI.
No network access is needed at run time.

## Windows (MSVC) - recommended

1. Install Visual Studio 2022 (or Build Tools) with the *Desktop development with C++* workload.
2. Install Qt 5.15.2 for `msvc2019_64` (Qt online installer, or `aqt install-qt windows desktop 5.15.2 win64_msvc2019_64`).
3. Open the *x64 Native Tools Command Prompt* and run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64
cmake --build build
ctest --test-dir build --output-on-failure
```

4. Deploy and build the installer:

```bat
mkdir deploy\AudioBridge
copy build\AudioBridge.exe deploy\AudioBridge\
copy build\AudioBridgeCLI.exe deploy\AudioBridge\
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt --release --no-translations deploy\AudioBridge\AudioBridge.exe
copy "%VCToolsRedistDir%\x64\Microsoft.VC143.CRT\*.dll" deploy\AudioBridge\
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DSourceDir=..\deploy\AudioBridge installer\AudioBridge.iss
```

The installer is written to `installer\Output\AudioBridge-Setup-x64.exe`.

The GitHub Actions workflow `.github/workflows/build.yml` performs exactly these steps on a
Windows runner, runs the test suite, performs a silent install/uninstall test, and publishes
the installer and a portable ZIP as build artifacts.

## Cross-compiling from Linux (MinGW-w64)

Used during development to build and check the Windows executables without a Windows machine.

```bash
sudo apt install mingw-w64 qtbase5-dev cmake ninja-build
pip install aqtinstall
aqt install-qt windows desktop 5.15.2 win64_mingw81 -O /opt/qt-win
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/qt-win/5.15.2/mingw81_64 \
      -DAUDIOBRIDGE_QT_HOST_TOOLS=/usr/lib/qt5/bin
cmake --build build-win
tools/deploy-mingw.sh build-win /opt/qt-win/5.15.2/mingw81_64 deploy/AudioBridge
```

`AUDIOBRIDGE_QT_HOST_TOOLS` points the build at the Linux `moc`/`uic`/`rcc` (all Qt 5.15.x
generators emit the same output format). MinGW executables link the C++ runtime statically.

## Linux development build (simulated devices)

The engine, tests and GUI also build on Linux. Without WASAPI, the GUI uses **simulated**
devices with generated test tones (clearly labelled in the UI). This is only for development
and UI work; it is never part of the Windows release.

```bash
cmake -S . -B build -G Ninja -DAUDIOBRIDGE_DEV_SIMULATION=ON
cmake --build build && ctest --test-dir build
./build/AudioBridge
```

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `AUDIOBRIDGE_BUILD_GUI` | ON | Qt application |
| `AUDIOBRIDGE_BUILD_CLI` | ON | `AudioBridgeCLI` |
| `AUDIOBRIDGE_BUILD_TESTS` | ON | test suite (`ctest`) |
| `AUDIOBRIDGE_DEV_SIMULATION` | OFF | non-Windows GUI with simulated devices |
| `AUDIOBRIDGE_QT_HOST_TOOLS` | empty | host moc/uic/rcc directory for cross builds |
