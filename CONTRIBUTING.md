# Contributing and preparing a source release

The current project overview and build instructions are in [readme.md](readme.md).
Detailed documentation starts at [documentation/index.md](documentation/index.md).
The project uses GPLv3; retain the existing license and author notices.

## Before committing

- Build the full application and the lean configuration shown in the README.
- Run `make -f tests/preferences/Makefile preferences-test` using an SDL2/MicroUI
  build. It uses dummy audio/video and writes fixtures under `_obj_`.
- Run `sh tests/sd_write/run.sh` and `bash tests/bootloader/run.sh` on a POSIX host.
- Run `python3 tools/source_audit.py`. It checks the proposed source file set
  for local runtime data, build artifacts, and recognizable credential formats.
- Review `git status --short` and `git diff --cached` before committing. Ignore
  rules do not remove files already tracked by Git. Use `git rm --cached` only
  for files you have reviewed and intend to stop tracking; keep local copies.

`config.cfg` is generated on first launch and can contain network credentials.
Do not publish it. Games, SD contents, saves, profiles, captures and local
backups are ignored. Keep them locally; redistribute game binaries separately
only when their licenses permit it. Built-in bootloader and embedded web asset
headers are source-distribution inputs and must remain included. Use the
explicit regeneration/check targets in the README when changing their inputs.

The source audit prints filenames and findings, never credential contents. It
is a focused check, not a guarantee that every sensitive value is detected.
Review staged changes yourself, including third-party code and asset licenses.

## GitHub setup

If this is a source archive without Git metadata, create an empty GitHub
repository, then initialize this folder locally with `git init -b main`.
Review the ignored file set with `git status --short --ignored`, run the audit,
and stage the intended source files. Make the initial commit after review.
Add your GitHub repository URL as `origin`, then push `main` when ready.
Existing repositories should retain their history, branch names and remotes.

The GitHub Actions workflow builds full and lean Linux configurations and runs
the preference, writable-SD and resident-bootloader regressions. Hardware,
Windows/macOS, browser, live network/MIDI, and real audio/video checks remain
separate validation tasks.
