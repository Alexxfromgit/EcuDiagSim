#!/usr/bin/env python3
"""Packs a built EcuDiagSim with Velopack: installer, portable build and update packages.

    python scripts/package.py --version 0.2.0 [--preset release] [--out dist] [--notes notes.md]

Steps: `cmake --install build/<preset>` into a staging folder (the app with Qt, the Velopack
library and the C++ runtime), then `vpk pack` for this platform:

    Windows  EcuDiagSim-win-x64-Setup.exe, -Portable.zip
    macOS    EcuDiagSim-osx-<arch>-Setup.pkg, -Portable.zip
    Linux    EcuDiagSim-linux-x64.AppImage

plus EcuDiagSim-<version>-<channel>-full.nupkg (and -delta.nupkg when the output folder already
holds the previous version) and the release feed releases.<channel>.json that installed copies
read to find updates. Needs the .NET SDK (vpk is restored from .config/dotnet-tools.json).
"""
import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "resources" / "icons"


def default_channel() -> str:
    machine = platform.machine().lower()
    arch = "arm64" if machine in ("arm64", "aarch64") else "x64"
    if sys.platform == "win32":
        return f"win-{arch}"
    if sys.platform == "darwin":
        return f"osx-{arch}"
    return f"linux-{arch}"


def run(cmd: list[str], **kwargs) -> None:
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, cwd=ROOT, **kwargs)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--version", required=True, help="package version, e.g. 0.2.0")
    p.add_argument("--preset", default="release", help="CMake build preset that was built (default: release)")
    p.add_argument("--channel", default=default_channel(), help="Velopack channel / runtime (default: %(default)s)")
    p.add_argument("--out", type=pathlib.Path, default=ROOT / "dist" / "velopack", help="vpk output folder")
    p.add_argument("--notes", type=pathlib.Path, help="release notes (Markdown) shown by the installer")
    p.add_argument("--stage", type=pathlib.Path, help="staging folder (default: build/<preset>-stage)")
    a = p.parse_args()

    build = ROOT / "build" / a.preset
    if not (build / "CMakeCache.txt").exists():
        print(f"{build} is not configured; build the '{a.preset}' preset first", file=sys.stderr)
        return 1
    stage = a.stage or ROOT / "build" / f"{a.preset}-stage"
    if stage.exists():
        shutil.rmtree(stage)
    run(["cmake", "--install", build, "--prefix", stage])

    pack_dir = stage
    if sys.platform == "darwin":
        pack_dir = stage / "EcuDiagSim.app"
    if not pack_dir.exists():
        print(f"nothing installed at {pack_dir}", file=sys.stderr)
        return 1

    run(["dotnet", "tool", "restore"])
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cmd = ["dotnet", "vpk", "pack",
           "--packId", "EcuDiagSim",
           "--packVersion", a.version,
           "--packDir", pack_dir,
           "--packTitle", "ECU Diagnostics Simulator",
           "--packAuthors", "Alexander Rubtsov",
           "--runtime", a.channel,
           "--channel", a.channel,
           "--outputDir", out]
    if a.notes:
        cmd += ["--releaseNotes", a.notes.resolve()]
    if sys.platform == "win32":
        cmd += ["--mainExe", "EcuDiagSim.exe", "--icon", ICONS / "ecudiagsim.ico"]
    elif sys.platform == "darwin":
        # vpk adds its updater to the bundle and then signs everything (--deep, hardened runtime,
        # Velopack's entitlements). Without a Developer ID that signature is ad hoc ("-"): Apple
        # silicon needs a valid signature to run at all, and a seal broken by files added after
        # signing makes macOS call the downloaded app "damaged". With an ad-hoc signature users
        # confirm the first start once (right-click > Open).
        identity = os.environ.get("ECUSIM_MAC_SIGN_IDENTITY", "-")
        cmd += ["--mainExe", "EcuDiagSim", "--icon", ICONS / "ecudiagsim.icns", "--signAppIdentity", identity]
    else:
        cmd += ["--mainExe", "EcuDiagSim", "--icon", ICONS / "ecudiagsim.png", "--categories", "Education"]
    run(cmd)

    print(f"\nPackages in {out}:")
    for f in sorted(out.iterdir()):
        if f.is_file():
            print(f"{f.stat().st_size:>12,}  {f.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
