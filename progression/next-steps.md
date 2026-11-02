# Next steps

## >>> START HERE: top priority for the next session <<<

**Gameplay video frame pacing (KI-14).** The guest swaps at a steady 60 fps and gameplay audio is
now clean (KI-13), but a 60 fps screen capture of gameplay shows only **~30 distinct frames per
second** plus small drops (menus: ~60). The host GPU (Radeon Renoir iGPU, already at its max
1500 MHz) is ~90% busy in gameplay. Plan, in order:

1. **Find where the GPU time goes.** Toggle SDK GPU options one at a time and measure each run
   the same way (see "How to measure" below). Candidates: `--direct_host_resolve`,
   `--readback_memexport false`, `--async_shader_compilation`, `--present_effect`,
   `--resolution_scale`, `--texture_cache_memory_limit_*`, `--vsync false`. Read
   `third_party/rexglue-sdk/src/graphics/vulkan/` (`command_processor.cpp` `IssueSwap`,
   `render_target_cache.cpp`) and `src/ui/presenter.cpp` (guest output mailbox) to see how
   frames are dropped when the GPU is late.
2. **Profile the GPU side**, not only the CPU: `perf` (`scripts/profile.sh`) only shows CPU.
   Consider RenderDoc (the SDK has `renderdoc_api.h`) or `RADV_DEBUG`/`radeontop` to see whether
   resolves, EDRAM transfers, texture uploads or the presenter dominate.
3. **Cheap CPU win, independent of the above:** the SDK audio worker burns ~80% of a core polling
   in `PosixConditionBase::WaitMultiple` (1 ms alertable slices + trylock). Not the bottleneck,
   but wasted CPU on a 6-core laptop.
4. Later: `-O2` for the generated code (currently `-O1` because of the 8 GB RAM limit) only if
   the guest CPU ever becomes the bottleneck; today the main thread spends ~75% of its time
   waiting for the GPU.

**How to measure** (every change, before and after):
- `scripts/run.sh --show_fps true --perf_log true` — guest fps on screen and in `build/run.log`.
- Distinct frames that actually reach the screen: record gameplay with
  `gpu-screen-recorder -w eDP-1 -f 60 -fm cfr -k h264 -o build/capture.mp4`, then count unique
  frames per second with `ffmpeg -i build/capture.mp4 -vf "scale=480:-1,mpdecimate=hi=64*4:lo=64*2:frac=0.1,showinfo" -an -f null -`
  (group the `pts_time` values by second). Run heavy analyses in the background.
- `scripts/profile.sh` — perf record + host GPU load (`build/gpu_busy.log`).
- Getting to gameplay needs the user with a gamepad (intro skip, party, song); plan runs so each
  one answers a specific question.

---

Suggested order, from cheapest/most informative to most expensive.

## 1. Validate what runs (immediate)
- [x] Run `scripts/run.sh` and record what appears. Result: the intro animated movie plays on screen.
- [x] Fix the random intro freeze/crash (KI-7, input race in the SDK).
- [ ] Record whether the intro has sound, whether it can be skipped, and what happens when it ends (title screen, black screen, crash). If it crashes, note the last `[FATAL]` line from `build/run.log`.

- [x] Song list after creating a party (KI-12, `XNetRandom`). A full song plays.
- [x] Gameplay audio crackling / slow-motion feel: SDL pulled 4 frames per callback, fixed with `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256` (KI-13).
- [ ] **Gameplay video frame pacing (top priority, KI-14):** see "START HERE" at the top of this file.
- [ ] Remove or gate the `--debug_probe` watch list once it is no longer needed (it only reads memory and is off by default).

## 2. Stability of the recompiled code
- [x] Remove bogus orphan entries that split switches/SEH functions (KI-8): 115 unresolved sites → 0.
- [ ] Identify the 4 functions still PENDING and add `[[switch_tables]]` if needed.
- [ ] Re-check the 52 remaining `orphan_*` entries for SEH resume blocks (harmless today, but dead).
- [ ] Run longer scenarios (menus, loading a song) and repeat the missing-function loop (`scripts/resolve_orphans.py`) whenever `unregistered function` shows up.
- [ ] Decide whether to generate a complete vtable report to anticipate missing functions instead of waiting for each crash.

## 3. Title update
- [ ] The game looks for `update:\gen\patch_xbox.hdr`. Investigate whether the TU in `game/$SystemUpdate/su20076000_00000000` is an XEXP that should be applied to `default.xex` (XEXLoaderWV supports XEXP) and whether it changes the code.
- [ ] If so, recompile from the patched XEX and rebuild the manifest.

## 4. Audio and video
- [ ] Confirm XMA decoding (music and SFX) and latency.
- [ ] Check the `Skipping Vulkan frame presentation due to async placeholder draw usage` message and whether it affects the picture.
- [ ] Bink video playback: the intro movie already shows; verify audio sync and the other movies in the game (`BINK*` blocks in the XEX).

## 5. Instruments (the distinguishing feature of this game)
- [x] Let a gamepad act as guitar/bass/drums (`--instruments`, KI-9).
- [ ] Map how the game reads guitar/bass/drums/microphone (USB HID through XInput / `XamInput*` / `XUsb*`?) and what the ReXGlue runtime already exposes.
- [ ] Keyboard mapping: right now only a real gamepad reaches the game (check the SDK's `mnk` input driver / `input_backend`).
- [ ] Microphone capture for vocals (3-part harmonies).

## 6. Persistence and extras
- [ ] Saves (XContent/STFS), profile and achievements.
- [ ] DLC / extra songs (STFS under `Content/`).

## 7. Project quality
- [ ] Push the repository to GitHub and pick a license.
- [ ] Document the manifest quirks discovered so far (see `AGENTS.md`) where other ReXGlue users can find them.
- [ ] Consider contributing fixes or reports back to ReXGlue (vtables without RTTI).
