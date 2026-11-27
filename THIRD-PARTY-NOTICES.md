# Third-party notices

ECU Diagnostics Simulator is licensed under the [MIT License](LICENSE). It uses the following
third-party software.

## Qt 6

The application links dynamically against the Qt 6 libraries (Qt Core, Qt Gui, Qt Qml, Qt Quick,
Qt Quick Controls, Qt SerialBus, optionally Qt WebEngine/Qt WebChannel, and their dependencies),
© The Qt Company Ltd. and other contributors,
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

## Three.js (optional 3D viewer)

The offline vehicle viewer bundles Three.js **0.180.0**, including OrbitControls,
© 2010–2025 Three.js Authors, under the **MIT License**. The complete license is in
`resources/vehicle-viewer/vendor/LICENSE` and is installed with 3D-enabled packages as
`licenses/three.js-LICENSE.txt` (inside `Contents/Resources` on macOS).
See `resources/vehicle-viewer/vendor/README.md` for the exact source and checksum.

The car geometry and generated environment lighting are original project assets under the
repository's MIT license. No purchased model, HDRI, font, texture, or Qt Quick 3D module is used.

Qt WebEngine contains Chromium and other third-party software under additional licenses;
see <https://doc.qt.io/qt-6.8/qtwebengine-licensing.html>. Its notices must be retained alongside
the Qt license texts. The app's MIT license does not replace these dependency licenses.

For distribution, the corresponding Qt/library sources or a compliant source offer must be
provided through an arrangement under the publisher's control; the upstream download link
above is a reference, not a substitute for fulfilling that requirement. See
<https://www.qt.io/faq/qt-open-source-licensing> and the release notes in `docs/vehicle-3d.md`.

## Standards

Protocol behaviour follows publicly documented descriptions of ISO 15765-2, ISO 14229-1, SAE J1979 and
SAE J2012. No text from these standards is reproduced. DTC descriptions are short paraphrases of the
generic SAE J2012 code meanings.
