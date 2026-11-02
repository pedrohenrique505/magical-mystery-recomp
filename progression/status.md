# Current status

_Last updated: 2026-11-02 (evening session)_

## Summary

The recompiled game is **playable end to end with a gamepad emulating a guitar** (or bass/drums, `--instruments`): intro, menus, party creation, song list and full songs, with clean audio (KI-13). Gameplay holds a **steady 60 fps** on the Vega 6 iGPU (Ryzen 5 4600H): frame pacing is locked to vsync (KI-14), and the camera-cut drops are gone since `execute_unclipped_draw_vs_on_cpu` stopped render-target transfers from copying the whole EDRAM (frames over the 16.7 ms GPU budget 7.9% → 0.02% in "Sgt. Pepper"; other scenes confirmed by the user). The Vulkan direct resolve (patch 0007) skips the EDRAM dump for ~90% of resolves. The 3D venue is drawn at 30 fps by the engine's design; `--full_rate_world` draws it every frame at ~54 fps (off by default). The game's own menus now have **Options → PC Settings** (venue rate, fullscreen, aspect ratio, internal resolution, texture filtering, FXAA) and **Quit to Desktop**, added by patching the game's DTA scripts at runtime without touching the dump (KI-16).

## Working

| Area | State |
|---|---|
| Disc extraction | ✅ `game/default.xex` (10.8 MB) + 4.4 GB of assets |
| Recompilation (`rexglue codegen`) | ✅ ~52.9k functions in 130 C++ files (~140 MB), ~2 min, ~3 GB peak |
| Build | ✅ ~51 MB binary, `-O1`, `-j2`, a few minutes; patched SDK runtime + Vulkan plugin via `scripts/build_sdk.sh` (patches 0001-0010) |
| Boot, intro, menus | ✅ intro movie plays and can be skipped (A); menus navigate with a gamepad; the intro input race is worked around (KI-7) |
| Party and song list | ✅ fixed `XNetRandom`/user GUIDs (KI-12) and the network receive loop (KI-11) |
| Gameplay | ✅ full songs with a gamepad as guitar/bass/drums (`--instruments`, KI-9) |
| Audio | ✅ clean in gameplay (KI-13), 6 channels at 48 kHz |
| GPU / frame rate | ✅ Vulkan (`xenos` plugin, RADV), steady 60 fps in gameplay, 0 dropped frames (KI-14) |
| PC Settings menu | ✅ Options → PC Settings and main menu → Quit to Desktop (user-validated); saved in `pc_settings.toml`; 2x internal resolution and FXAA work, 2x is too heavy for 60 fps on the Vega 6 (KI-17) |
| Game script mods | ✅ `scripts/build_game_overlay.py` → `build/game_overlay/` (new ark part + header, script SHA-1 table patched by the host) |
| Tooling | ✅ PyGhidra in `.venv`, missing-function scripts, per-frame GPU logs (`--gpu_frame_csv`), perf/RGP/RenderDoc capture scripts |

## Not done / pending

- Real USB instruments (guitar, drums, microphone for vocals): nothing implemented or tested.
- Keyboard input: does nothing (only a real gamepad reaches the game).
- Title update device (`update:\`) missing (KI-2).
- Saves/XContent, DLC, achievements, Live.
- 4 functions still PENDING in the analysis (KI-1).
- `--full_rate_world` misses 60 fps (~54) on the Vega 6; the rest is mostly the game's own drawing.
- Internal resolution 2x2 runs at ~28 fps and 2x1 at ~50 fps (KI-17); with patch 0010 the menu's "2x Horizontal" (2x1, sharp) runs at 56.2 fps, not yet a stable 60.
- PC Settings strings are English in every language; FSR/CAS need an SDK build with FidelityFX (not available).

## Useful numbers

- Manifest: 52 `orphan_*`, 42 `codeptr_*` and 2 `thunk_*` entries in `[entrypoint.functions]`, plus 2 `[[entrypoint.midasm_hook]]`s (`ProcCounter::ProcCommands`, `DataArray::Execute` unknown-command path for `{pc_cfg ...}`). Generated code has 0 unresolved-branch sites; 4 functions still PENDING.
- Title: ID `454108B1`, XEX version 0.0.0.1 (no title update applied), filetime 2009-06-15.
- Environment: 8 GB RAM, clang 23, Ghidra 12.1.2, Java 27, Python 3.14.
- SDK patches (`patches/rexglue-sdk/`): 0001-0002 presenter, 0003 GPU command counters + plugin dir, 0004 upload coalescing (off), 0005 busy-wait fix, 0006 per-frame GPU timing (`--gpu_frame_csv`), 0007 Vulkan direct host resolve (`--direct_host_resolve`, on), 0008 `swap_post_effect` (FXAA) switchable at runtime, 0009 direct resolve with resolution scaling (`--direct_host_resolve_scaled`, on), 0010 per-surface resolution scale (`--draw_resolution_scale_threshold`, off by default).
