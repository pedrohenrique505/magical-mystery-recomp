# Next steps

## >>> START HERE: top priority for the next session <<<

**Gameplay hitches (KI-14, second half).** The presenter no longer drops frames: since the
achievement toast was disabled, every guest frame is painted (`--perf_log`: painted = refreshed,
0 dropped). What is left happens *in the guest*: one `VdSwap` interval of 40-60 ms (up to 150 ms)
every 2-6 s, plus short stretches of 23-40 ms frames. They do not line up with pipeline creation.
Plan, in order:

1. **Timestamp each hitch.** Log every swap interval > 30 ms with a timestamp (perf_hooks.cpp)
   and correlate with `--wait_profile` (file reads, kernel waits per guest thread), the SDK
   command processor's waits for GPU submissions, and texture uploads. Find out whether the main
   thread was blocked (I/O, a lock, the GPU) or busy.
2. **Reduce GPU load** if the hitches turn out to be GPU waits: the iGPU is still the busiest part.
   Options to A/B (remember `--no-name` to turn a boolean off): `--direct_host_resolve`,
   `--no-readback_memexport`, `--async_shader_compilation`, `--resolution_scale`,
   `--texture_cache_memory_limit_*`, `--present_effect`. Consider RenderDoc
   (`renderdoc_api.h` in the SDK) to see what dominates a gameplay frame.
3. **Present pacing:** the swapchain uses IMMEDIATE (tearing allowed, no vsync). Try FIFO
   (`--no-vulkan_allow_present_mode_immediate --no-vulkan_allow_present_mode_mailbox
   --no-vulkan_allow_present_mode_fifo_relaxed`); FIFO counts as implicit vsync, which moves
   painting back to the UI thread, so check the dropped counter.
4. **Cheap CPU win:** the SDK audio worker burns ~80% of a core polling in
   `PosixConditionBase::WaitMultiple` (1 ms alertable slices + trylock).
5. Later: `-O2` for the generated code only if the guest CPU becomes the bottleneck.

**How to measure** (every change, before and after):
- `scripts/run.sh --perf_log true` — every 2 s: guest fps and swap-interval histogram, audio
  pacing, and the host side (`refreshed / painted / new / dropped`, new-frame intervals).
  At a clean 60 fps everything is in the `10-23` bucket. **Do not use `--show_fps` or F3/F4
  while measuring**: ImGui overlays switch the presenter back to UI-thread painting.
- The game window must be visible: Hyprland throttles hidden windows, which skews present
  numbers (an intro-only run without user input is fine if the window is on screen).
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
- [x] Gameplay showed ~30 distinct fps: the presenter dropped half the frames because the SDK's achievement toast forced UI-thread painting (KI-14).
- [ ] **Gameplay hitches (top priority, KI-14):** see "START HERE" at the top of this file.
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
