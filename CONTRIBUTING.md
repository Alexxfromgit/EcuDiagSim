# Contributing to ECU Diagnostics Simulator

Thanks for helping! Bug reports, lesson ideas and pull requests are all welcome.

## Getting started

You need CMake ≥ 3.24, Ninja, a C++20 compiler and Qt ≥ 6.8 with the *Qt SerialBus* add-on:

- **Windows:** Visual Studio 2022 or newer with *Desktop development with C++*, Qt for MSVC 2022 x64.
- **Linux:** for example `sudo apt-get install -y build-essential cmake ninja-build libgl1-mesa-dev libxkbcommon-dev`,
  Qt for `linux_gcc_64`.

Qt can be installed without an account with [aqtinstall](https://github.com/miurahr/aqtinstall), see the
[README](README.md#build-and-run). Then:

```bash
./scripts/build.ps1 -Test          # Windows
./scripts/build.sh --test          # Linux
```

See [docs/architecture.md](docs/architecture.md) for how the simulator is put together.

## Pull requests

- Keep each PR to one change and describe what it does and how you tested it.
- Title the PR as a [Conventional Commit](https://www.conventionalcommits.org): PRs are
  squash-merged, and the title becomes the commit that drives versioning and release notes.
  - `feat: add a misfire lesson` → new feature, minor release
  - `fix: keep the scan tool session after tester present`, `perf: …`, `deps: …` → patch release
  - `feat!: …`, or a `BREAKING CHANGE:` line in the description → breaking change
  - `docs:`, `refactor:`, `test:`, `build:`, `ci:`, `chore:` → no release on their own, not in the notes

  Write the subject for users: it shows up as-is in the changelog.
- Add or update tests in `tests/` (Qt Test). CI runs them on Windows (MSVC) and Linux
  (GCC with AddressSanitizer and UndefinedBehaviorSanitizer). Builds must stay warning-free:
  the `ci` preset treats warnings as errors.
- Follow the existing style; `.clang-format` and `.editorconfig` cover formatting.

## Keeping the simulation honest

This is a teaching tool, so what it shows has to be right:

- **Deterministic.** `src/core` never reads the wall clock, uses random numbers only from seeded
  generators and never lets hash ordering influence results. `tst_vehicle::deterministic` checks it.
- **Standards first.** Protocol behaviour (ISO 15765-2, ISO 14229-1, SAE J1979, SAE J2012) should
  follow the standard. Where the simulator simplifies on purpose, say so in the docs.
- **One source of truth.** CAN signals are defined only in `resources/dbc/vehicle.dbc`; DTC texts only in
  `src/core/diag/DtcCatalog.cpp`. Update `docs/` when you change either.
- **Lessons are tests.** Every scenario in `resources/scenarios/` runs in `tst_scenarios`; a new lesson
  needs a guide in `docs/lessons/`.

## Releases

[Release Please](https://github.com/googleapis/release-please) keeps a release PR open on `main`
that bumps the version in `CMakeLists.txt` and updates `CHANGELOG.md` from the PR titles. Merging it
tags `vX.Y.Z`; the *Release* workflow then builds and tests the Windows package, attaches it with a
checksum and build provenance, and publishes the release.

To build the assets for a tag by hand (for example the very first release), push the tag, create a
draft release for it and run the *Release* workflow with that tag (a draft release alone does not
create the tag, and the workflow checks the tag out):

```bash
git tag v0.1.0 && git push origin v0.1.0
gh release create v0.1.0 --draft --verify-tag --title v0.1.0 --notes-file CHANGELOG.md
gh workflow run release.yml -f tag=v0.1.0
```

## Reporting bugs

Use the issue templates. For security problems, follow [SECURITY.md](SECURITY.md) instead of
opening a public issue.
