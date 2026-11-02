# Next steps

## >>> START HERE: top priority for the next session <<<

**Gameplay runs at ~58 fps with host render targets (now the default); the "40-45 fps feel"
is the game's half-rate venue (KI-14).** A screen capture shows the 3D venue redrawn every
other frame and only the note highway every frame, matching the heavy/light GPU frames seen in
RGP. Decide with the user:

1. **Force the venue to full rate**, as the RB2/RB3 "Deluxe" mods do ("60 fps venues"): find
   the engine switch (RB2 symbol dump `third_party/rb3/doc/rb2_dump/`, Ghidra; look at how
   the world/venue draw is skipped on alternate frames) and patch it from the host app. GPU
   cost roughly doubles for the venue: measure whether the Vega 6 can hold 60 (heavy frames
   are the limit; on fbo the GPU averaged ~80% busy at 58 fps with half-rate venue).
2. Remaining dips (40-46 fps in a short stretch on fbo): profile with `scripts/rgp_capture.sh`
   on the fbo path; candidates are the per-draw shared-memory uploads (barrier +
   `vkCmdCopyBuffer` before small draws, `VulkanSharedMemory::UploadRanges`).

**How to measure:** `RUN_LOG=build/runX.log scripts/run.sh --perf_log=true --hitch_log_ms=35`
for guest fps / GPU; for what the screen really shows, record with
`gpu-screen-recorder -w eDP-1 -f 60 -fm cfr -k h264 -q ultra -cursor no -s 960x540 -o build/capX.mp4`
and run `scripts/count_distinct_frames.py build/capX.mp4` (in the background; the per-cell
difference map in `progression/log.md` 2026-11-02 explains how highway-only frames were found).
GPU profiles: `scripts/rgp_capture.sh N` (auto-captures sustained slow stretches,
`touch build/rgp_trigger` for a manual one), open with RGP 1.14.1. Gameplay runs need the user.

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
