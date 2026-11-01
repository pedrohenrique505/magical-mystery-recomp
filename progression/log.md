# Development log

## 2026-11-01 — From zero to a running game

- **Decision:** static recompilation (not a matching decomp). Use the **prebuilt ReXGlue SDK** instead of building the SDK (8 GB of RAM). XenonRecomp/XenosRecomp/rb3 are kept as reference only.
- Disc extracted with `extract-xiso`; `default.xex` (title `454108B1`).
- `rexglue init` + `codegen` failed on the first attempt with 1 error: `b 0x8264E3F0` to a 4-byte thunk (`b 0x8264E060`) that the analysis did not recognize.
  - Declaring the function **without `size`** did not help; the real reason was that `[functions]` at the manifest root is **silently ignored**. The correct table is `[entrypoint.functions]`.
- Installed the XEXLoaderWV loader in Ghidra (version warning; needed Jython enabled). Used Ghidra to disassemble the thunk.
- Build: `clang` + `mold` + `ccache`, `-O1`, `-j2`; the presets ask for `clang-20`, which is not installed on the machine.
- First run: no GPU (`gpu_plugin not set`). Fixed with `GPU_PLUGINS xenos` in CMake **and** `--gpu_plugin xenos` on the command line.
- Successive `Call to invalid or unregistered function` crashes (`0x82648980`, `0x828807E8`): vtable thunks without RTTI. Instead of fixing one per round, installed **PyGhidra** (already bundled with Ghidra, wheels in `Features/PyGhidra/pypkg/dist`) and wrote `scripts/find_orphans.py`, which lists data targets with no registered function (252 candidates).
  - Filter candidates that fall inside another candidate's range (interior labels) and follow `b` chains (thunks that jump to another stub).
  - Ghidra is **not** a good oracle for what rexglue covers (non-contiguous function bodies); the `codegen` validator is. Hence `scripts/resolve_orphans.py`.
- Result: 155 functions added, clean `codegen`, the game runs for 60 s without aborting.
- Created `CLAUDE.md`, `README.md` and this folder.
