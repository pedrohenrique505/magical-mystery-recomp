# Current status

_Last updated: 2026-11-02_

## Summary

The recompiled game **boots, plays the intro, navigates menus, creates a party, lists songs and plays a full song** with a gamepad emulating a guitar. Gameplay audio is clean (KI-13). Frame pacing is fixed (KI-14): every guest frame is shown exactly once, locked to the display vsync, and the FSI render-target path cut GPU load; songs average ~59 fps, but two heavy scenes (venue fly-through, sunflower field with the crowd) drop to ~37-40 fps. The next fix needs a rebuilt SDK (presenter paints from the UI thread under FIFO; see `next-steps.md`).

## Working

| Area | State |
|---|---|
| Disc extraction | ✅ `game/default.xex` (10.8 MB) + 4.4 GB of assets |
| Recompilation (`rexglue codegen`) | ✅ ~52.9k functions in 130 C++ files (~140 MB), ~2 min, ~3 GB peak |
| Build | ✅ ~51 MB binary, `-O1`, `-j2`, a few minutes |
| Runtime boot | ✅ memory arena, function table, SDL input |
| GPU | ✅ `xenos` plugin on Vulkan (AMD RADV Renoir); 1920×1080 swapchain; shaders and pipelines created |
| Audio | ✅ endpoint initializes (6 channels, 48 kHz) |
| Intro movie | ✅ visible on screen; the random freeze/crash during it was an input race in the SDK, worked around in the host app (KI-7). 120 s runs no longer stall; visual confirmation pending |
| Tooling | ✅ PyGhidra in `.venv`; scripts to find missing functions |

## Not validated / pending

- Menus: ✅ navigable. Party creation and the song list work (KI-12); full songs play with `--instruments guitar` (default). Gameplay: 0 dropped frames, ~59 fps mean, 6.5% of 2 s windows below 55 fps (KI-14); the pre-song venue fly-through still runs at ~49 fps. GPU headroom is the open item.
- 4 functions still PENDING in the analysis — see [`known-issues.md`](known-issues.md) KI-1.
- Input: a gamepad works (A skips the intro, menus navigate); the keyboard does nothing. A gamepad counts as a vocalist unless `--instruments` is used (KI-9); playing a song not yet confirmed.
- Title update device (`update:\`) missing.
- USB instruments (guitar, drums, microphone) — nothing implemented or tested.
- Saves/XContent, DLC, achievements, Live.

## Useful numbers

- Manifest: 52 `orphan_*`, 42 `codeptr_*` and 2 `thunk_*` entries in `[entrypoint.functions]`. Generated code has 0 unresolved-branch sites; 4 functions still PENDING.
- Title: ID `454108B1`, XEX version 0.0.0.1 (no title update applied), filetime 2009-06-15.
- Environment: 8 GB RAM, clang 23, Ghidra 12.1.2, Java 27, Python 3.14.
