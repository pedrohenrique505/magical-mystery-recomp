# Next steps

## >>> START HERE: top priority for the next session <<<

**Build the ReXGlue SDK from source to fix presentation under FIFO (KI-14).** Everything the
host app can change has been tried (see KI-14 "Tested on 2026-11-02 without gain"). Songs run at
~59 fps, but two scenes stay slow: the pre-song venue fly-through (pure GPU limit) and the
sunflower field with the crowd (~40 fps). In the sunflower scene the GPU idles ~15% under FIFO
because the SDK paints from the UI thread behind the game's GPU work; MAILBOX (GPU thread
presents) reaches ~47-58 fps there but loses frame pacing. The functions that choose the paint
mode are internal to `librexruntime`, so the fix needs a rebuilt SDK.

1. **Build the SDK** (`third_party/rexglue-sdk`) with the same toolchain flags, `-j2`, `nice`,
   check peak RAM first (8 GB machine). Point `CMAKE_PREFIX_PATH` at the new install and confirm
   the game still runs identically before changing anything.
2. **Presenter change:** let the guest output (GPU command) thread present even with a FIFO
   swapchain (`Presenter::GetDesiredPaintModeFromUIThread`, `src/ui/presenter.cpp`), so the
   vsync wait happens in that thread and replaces `--present_pacing`; or keep MAILBOX and pace on
   real display timing with `VK_KHR_present_wait` (check RADV/Wayland support). Measure the
   sunflower scene: target is GPU ~99% busy with no dropped frames.
3. **Then GPU cost:** with a debug-marker build, look at the small draws that take 0.7-1.5 ms on
   the FSI path (shadow-map passes) for barriers/stalls in the Vulkan command processor.
4. Smaller items: the SDK audio worker polls `WaitMultiple` at ~80% of a core (shares the APU
   power budget); the venue fly-through is GPU-bound at 99%.

If building the SDK is not wanted, the alternative is accepting the current state (FIFO + pacing +
FSI defaults; optionally MAILBOX for slightly higher fps in heavy scenes at the cost of pacing).

**How to measure** (every change, on AC power, same song):
- `scripts/run.sh --perf_log=true --hitch_log_ms=35`: per 2 s guest fps, swap histogram,
  presenter refreshed/painted/new/dropped, pacing timeouts, GPU busy/clock/temperature/PPT.
  Compare song segments with the summary script approach used on 2026-11-02 (mean shown fps,
  share of windows < 55 fps). No `--show_fps`/F3/F4 while measuring.
- `amdgpu_top -J -s 250` in the background records per-block GPU counters (installed).
- The intro is **not** a valid GPU benchmark (a Bink movie; flags that matter in gameplay do
  nothing there). Gameplay runs need the user; announce automated runs ("don't touch").

---

Suggested order, from cheapest/most informative to most expensive.

## 1. Validate what runs (immediate)
- [x] Run `scripts/run.sh` and record what appears. Result: the intro animated movie plays on screen.
- [x] Fix the random intro freeze/crash (KI-7, input race in the SDK).
- [ ] Record whether the intro has sound, whether it can be skipped, and what happens when it ends (title screen, black screen, crash). If it crashes, note the last `[FATAL]` line from `build/run.log`.

- [x] Song list after creating a party (KI-12, `XNetRandom`). A full song plays.
- [x] Gameplay audio crackling / slow-motion feel: SDL pulled 4 frames per callback, fixed with `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256` (KI-13).
- [x] Gameplay showed ~30 distinct fps: the presenter dropped half the frames because the SDK's achievement toast forced UI-thread painting (KI-14).
- [x] Gameplay frame pacing (KI-14): `--present_pacing` + FIFO, 0 dropped frames.
- [x] GPU: FSI render-target path removes EDRAM transfer draws (KI-14).
- [ ] **More GPU headroom (top priority, KI-14):** see "START HERE" at the top of this file.
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
