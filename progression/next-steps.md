# Next steps

Suggested order, from cheapest/most informative to most expensive.

## 1. Validate what runs (immediate)
- [ ] Run `scripts/run.sh` and record what appears: black screen, Harmonix/Apple Corps logos, video, menu, sound.
- [ ] Note the result in `log.md` and, if it crashes, the last `[FATAL]` line from `build/run.log`.

## 2. Stability of the recompiled code
- [ ] Handle the 11 `PENDING` functions with unresolved jumps (likely `switch` / jump tables). Find out which ones they are (see `known-issues.md`) and declare `[[switch_tables]]` in the manifest.
- [ ] Run longer scenarios (menus, loading a song) and repeat the missing-function loop (`scripts/resolve_orphans.py`) whenever `unregistered function` shows up.
- [ ] Decide whether to generate a complete vtable report to anticipate missing functions instead of waiting for each crash.

## 3. Title update
- [ ] The game looks for `update:\gen\patch_xbox.hdr`. Investigate whether the TU in `game/$SystemUpdate/su20076000_00000000` is an XEXP that should be applied to `default.xex` (XEXLoaderWV supports XEXP) and whether it changes the code.
- [ ] If so, recompile from the patched XEX and rebuild the manifest.

## 4. Audio and video
- [ ] Confirm XMA decoding (music and SFX) and latency.
- [ ] Check the `Skipping Vulkan frame presentation due to async placeholder draw usage` message and whether it affects the picture.
- [ ] Bink videos (`BINK*` blocks in the XEX).

## 5. Instruments (the distinguishing feature of this game)
- [ ] Map how the game reads guitar/bass/drums/microphone (USB HID through XInput / `XamInput*` / `XUsb*`?) and what the ReXGlue runtime already exposes.
- [ ] Keyboard/gamepad mapping as a minimal playable solution.
- [ ] Microphone capture for vocals (3-part harmonies).

## 6. Persistence and extras
- [ ] Saves (XContent/STFS), profile and achievements.
- [ ] DLC / extra songs (STFS under `Content/`).

## 7. Project quality
- [ ] Push the repository to GitHub and pick a license.
- [ ] Document the manifest quirks discovered so far (see `AGENTS.md`) where other ReXGlue users can find them.
- [ ] Consider contributing fixes or reports back to ReXGlue (vtables without RTTI).
