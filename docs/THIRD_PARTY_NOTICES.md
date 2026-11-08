# Third-party components

AudioBridge's own code has no third-party dependencies. The Windows distribution
ships the following components unmodified:

## Qt 5.15 (Qt Core, Qt GUI, Qt Widgets, Windows platform plugin)

- Copyright (C) The Qt Company Ltd. and other contributors.
- Used under the GNU Lesser General Public License v3 (LGPLv3).
- AudioBridge links Qt dynamically (`Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`,
  `platforms\qwindows.dll`, `styles\qwindowsvistastyle.dll`). You may replace these
  DLLs with your own build of the same Qt version.
- License text: https://www.gnu.org/licenses/lgpl-3.0.html
- Source code: https://download.qt.io/archive/qt/5.15/

## MinGW-w64 / GCC runtime (MinGW builds only)

- `libgcc_s_seh-1.dll`, `libstdc++-6.dll`: GCC runtime libraries, GPLv3 with the
  GCC Runtime Library Exception.
- `libwinpthread-1.dll`: MinGW-w64 winpthreads, MIT-style license.

## Microsoft Visual C++ runtime (MSVC builds only)

- `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`: redistributed under the
  Microsoft Visual C++ redistributable license terms.
