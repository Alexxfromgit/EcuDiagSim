# Third-party notices

ECU Diagnostics Simulator is licensed under the [MIT License](LICENSE). It uses the following
third-party software.

## Qt 6

The application links dynamically against the Qt 6 libraries (Qt Core, Qt Gui, Qt Qml, Qt Quick,
Qt Quick Controls, Qt SerialBus and their dependencies), © The Qt Company Ltd. and other contributors,
used under the terms of the **GNU Lesser General Public License version 3** (LGPLv3).

- License text: <https://www.gnu.org/licenses/lgpl-3.0.html> (and the GPLv3 it builds on,
  <https://www.gnu.org/licenses/gpl-3.0.html>)
- Qt licensing: <https://www.qt.io/licensing/open-source-lgpl-obligations>
- Source code of Qt: <https://download.qt.io/official_releases/qt/> — the release packages are
  built with the Qt version named in `.github/workflows/release.yml`.

The Qt libraries in the packages are unmodified and shipped as separate shared libraries (DLLs,
`.so` files, macOS frameworks), so you can replace them with your own build of the same Qt version.

Qt itself contains third-party code under various permissive licenses; see
<https://doc.qt.io/qt-6/licenses-used-in-qt.html>.

## Velopack

Installers, portable builds and in-app updates use [Velopack](https://github.com/velopack/velopack)
(the `velopack_libc` library shipped with the app and the `vpk` packaging tool),
© 2021 Caelan Sayler, © 2024 Velopack Ltd., under the **MIT License**:
<https://github.com/velopack/velopack/blob/develop/LICENSE>.

## Standards

Protocol behaviour follows publicly documented descriptions of ISO 15765-2, ISO 14229-1, SAE J1979 and
SAE J2012. No text from these standards is reproduced. DTC descriptions are short paraphrases of the
generic SAE J2012 code meanings.
