# Known issues

## KI-1 — 11 functions with unresolved jumps
- **Symptom:** `codegen` logs `Analyze: 11 functions still PENDING with unresolved jumps` (debug level, non-blocking).
- **Probable cause:** jump tables (`switch`) the analysis could not resolve.
- **Risk:** code that executes those jumps may fail at runtime.
- **How to investigate:** run `rexglue --log-level trace codegen ...`, find which functions stay `PENDING`, read the code in Ghidra and declare `[[switch_tables]]`. Manifest options are in `third_party/rexglue-sdk/src/codegen/config.cpp`.

## KI-2 — `update:\gen\patch_xbox.hdr` not found
- **Symptom:** `VFS: 'update:\gen' -> [no device]` and `NtCreateFile FAILED ... 0xc000000f` at the start of every run.
- **State:** has not blocked anything so far; the game continues.
- **Hypothesis:** the game looks for the title-update patch; the TU is in `game/$SystemUpdate/`.
- **See:** `next-steps.md` §3.

## KI-3 — Functions reachable only through vtables without RTTI
- **Symptom:** `[FATAL] Call to invalid or unregistered function at guest address 0x...` at runtime.
- **Cause:** ReXGlue's `VTableScanner` only finds vtables through MSVC RTTI; this game has many classes without RTTI.
- **Current mitigation:** `scripts/find_orphans.py` + `scripts/resolve_orphans.py` (155 functions already declared). New cases may appear in parts of the game not yet exercised.
- **Known limit:** the computed size runs to the first `blr`/`bctr`/`b`, so it is only reliable for thunks (≤ 32 bytes); larger functions are declared without `size` and rely on the SDK's analysis.

## KI-4 — Kernel/audio stubs
- **Symptom:** `__imp__XAudioOverrideSpeakerConfig STUB` in the log.
- **Impact:** unknown; verify that audio output is correct.

## KI-5 — Frame skipped because of `async placeholder draw`
- **Symptom:** `Skipping Vulkan frame presentation due to async placeholder draw usage in this frame` on the first frame.
- **Impact:** unknown; may just be the first frame while shaders load.

## KI-6 — XEXLoaderWV extension built for a different Ghidra version
- **State:** the fork's zip targets Ghidra 12.0.4; it was installed on 12.1.2 with "Install Anyway" and works, but required enabling the **Jython** extension (the loader references `org.python.jline`).
- **Plan B:** build from `third_party/XEXLoaderWV` with Gradle against the installed Ghidra.
