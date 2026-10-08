# CUzeBox documentation

This directory is the editable source for project documentation.

The goal is simple:

- keep the source format easy to edit in any text editor
- keep it friendly to Git diffs and code review
- make it easy to generate a nicer static site later

These pages are starter docs. They are meant to give the project a clean structure now, with enough concrete detail to be useful, while leaving room to expand as features stabilize.

## Recommended workflow

- edit the Markdown files in `documentation/`
- keep screenshots in `documentation/assets/images/`
- use short focused pages instead of one giant document
- prefer examples over prose where possible

## Suggested site generator

A starter `mkdocs.yml` is included so the documentation can be rendered as a static site later with MkDocs Material.

Example local workflow:

```bash
pip install mkdocs-material
mkdocs serve
```

That is optional. The Markdown files are still useful on their own in GitHub or any editor.

## Page map

- [Overview](overview.md)
- [Building](building.md)
- [SPI RAM](spiram.md)
- [Input Topology and Virtual Devices](input-topology.md)
- [Video Filters](video-filters.md)
- [Sound Options](sound.md)
- [Messages and Debugging Output](logging.md)
- [Preference Persistence](preference-persistence.md)
- [Cheats](cheats.md)
- [Serial and Endpoints](serial.md)
- [ESP8266 Emulation](esp8266-emulation.md)
- [Netplay](netplay.md) - starts with user setup and troubleshooting, then developer details
- [Debugger](debugger.md)
- [Automation API Profiler](api-profiler.md)
- [Automation API Disassembly and Run Control](api-run-control.md)
- [Automation API Stack Inspection](api-stack.md)
- [Automation API Memory Snapshots](api-memory-snapshots.md)
- [Automation API Memory Scans](api-memory-scans.md)
- [API Overview](api/index.md)
- [API Command Reference](api/commands.md)
- [API Automated Testing](api/testing.md)
