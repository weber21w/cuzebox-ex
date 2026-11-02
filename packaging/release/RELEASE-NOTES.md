CUEX native download packages, built from the same tagged source.

- Windows x86-64 ZIP: extract the whole CUEX folder and run cuzebox.exe.
- Linux x86-64 AppImage: make executable and run; tar.gz includes AppRun as a fallback.
- macOS arm64 / x86-64 ZIP: extract CUEX.app. Initial macOS packages target macOS 15 or newer and are ad-hoc signed test builds, without Developer ID signing or notarization. Gatekeeper may require explicit approval.

SDL2/runtime libraries and the FFmpeg recording helper are included. No game ROMs, personal configuration, credentials, saves or recordings are included.

Windows ZIPs default to portable storage. Linux/macOS use the platform CUEX user-data folder. Use --data-dir to select another writable folder; --print-paths reports resolved locations. Existing configurations and user-edited theme files are preserved. An explicitly loaded ROM's containing directory remains its virtual SD root.

Each platform includes SHA256SUMS and BUILDINFO source/binary provenance. The source archive includes the release build scripts and pinned FFmpeg/tool inputs. macOS desktop, controller and physical serial-device testing remains required before promoting test packages to a supported stable Mac release.
