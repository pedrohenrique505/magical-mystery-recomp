# Current status

_Last updated: 2026-11-01_

## Summary

The recompiled game **starts and runs without aborting** (tested for 60 s). Vulkan (Xenos) and audio initialize; actual gameplay has **not yet been visually validated**.

## Working

| Area | State |
|---|---|
| Disc extraction | ✅ `game/default.xex` (10.8 MB) + 4.4 GB of assets |
| Recompilation (`rexglue codegen`) | ✅ ~52.9k functions in 130 C++ files (~140 MB), ~2 min, ~3 GB peak |
| Build | ✅ ~51 MB binary, `-O1`, `-j2`, a few minutes |
| Runtime boot | ✅ memory arena, function table, SDL input |
| GPU | ✅ `xenos` plugin on Vulkan (AMD RADV Renoir); 1920×1080 swapchain; shaders and pipelines created |
| Audio | ✅ endpoint initializes (6 channels, 48 kHz) |
| Tooling | ✅ PyGhidra in `.venv`; scripts to find missing functions |

## Not validated / pending

- What the window actually shows (black screen, logos, video, menu) — **needs a visual test**.
- Functions with unresolved jumps (11) — see [`known-issues.md`](known-issues.md).
- Title update device (`update:\`) missing.
- USB instruments (guitar, drums, microphone) — nothing implemented or tested.
- Saves/XContent, DLC, achievements, Live.

## Useful numbers

- Manifest: 155 `orphan_*` entries + 2 `thunk_*` entries in `[entrypoint.functions]`.
- Title: ID `454108B1`, XEX version 0.0.0.1 (no title update applied), filetime 2009-06-15.
- Environment: 8 GB RAM, clang 23, Ghidra 12.1.2, Java 27, Python 3.14.
