# macOS validation checkpoint - November 2, 2026

Tested source: `a936a8cd3e3ae1fed473ab7c0733db7ec76e02af` on `codex/prepush-review-20261101`.
The Mac workflow [passed for both architectures](https://github.com/weber21w/cuzebox-ex/actions/runs/41405584268). Download the
`cuex-macos-arm64-unsigned` or `cuex-macos-x86_64-unsigned` artifact from that run;
each contains the app ZIP, matching source archives and SHA256SUMS.
The [Linux and browser regression workflow](https://github.com/weber21w/cuzebox-ex/actions/runs/41405584238) also passed.

Both native Mac runners passed core/GUI regressions, Timer1/GPIO/interrupt and
UART timing, storage and save-state compatibility, interlace, screenshot GUI
and API behavior, and independent PNG/video decoding. Extracted app tests
passed with a clean PATH, bundled FFmpeg recording, a real Cocoa-window launch,
API/PNG operation, clean quit, signature verification and unchanged signed
bundle contents. SDL audio used its dummy driver; physical audio, controllers,
MIDI and serial devices still need desktop testing.

The apps target macOS 15 or newer, separately for Apple Silicon and Intel.
They are ad-hoc signed test builds, without Developer ID signing/notarization.
The release workflow reuses the same Mac jobs. Nothing was merged into main,
tagged or published as an official release at this checkpoint.

Portability fixes retain existing features: Mac SDL header discovery, portable
ICMP echo headers, helper-before-app signing, and inclusion of the dynamically
loaded SDL3 runtime when the SDK provides sdl2-compat. Final FFmpeg muxing now
retains every submitted video frame instead of truncating the last frame when
rounded audio duration is slightly shorter. Both FFmpeg 4 and 9 decode the
full two/three-frame regression clips. A rebuilt Windows distribution/package
also passed headless API, startup/data isolation, PNG and bundled recording.
The longer pacing test passes on Windows/Linux and rejects an explicitly
unthrottled negative control; it passed on both Mac runners too.

See [release instructions](releases.md) for local GNU make/Homebrew commands,
artifacts, writable data and packaging. FFmpeg cache keys include architecture,
SDK/x264 version and the pinned helper recipe. Diagnostic artifacts are kept
separate from release downloads.
