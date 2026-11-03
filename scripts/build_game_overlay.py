#!/usr/bin/env python3
"""Builds build/game_overlay/: the game data directory with our script changes applied.

The game reads its scripts (.dtb) from the Harmonix archive gen/main_xbox_*.ark, indexed
by the encrypted header gen/main_xbox.hdr. This script never touches game/. It writes:

  build/game_overlay/<entry>          symlink to game/<entry> for everything but gen/
  build/game_overlay/gen/*.ark        symlinks to the original archive parts
  build/game_overlay/gen/main_xbox_10.ark   new part holding the modified .dtb files
  build/game_overlay/gen/main_xbox.hdr      header whose entries for those files point
                                            into the new part
  build/game_overlay/checksums.txt          SHA-1 of each patched file: the executable
                                            verifies scripts against a built-in table,
                                            which project/src/game_checksums.cpp updates

scripts/run.sh uses the overlay as --game_data_root when it exists. The changes come
from DTA snippets in project/mods/ (our own content, see the files for details):

  main_menu.dta    "Quit to Desktop" in the main menu (and its confirmation dialog) and
                   "PC Settings" in the Options popup
  pc_settings.dta  the PC Settings screen, appended to ui/options/gen/options_console.dtb
  locale.dta     strings for the new entries, added to every language
  venue_detail.dta  venue detail knobs (crowd density/LOD, depth of field, self-shadows)
                   read from host cvars at every camera shot, in world/gen/world_objects.dtb

Usage: scripts/build_game_overlay.py [--game game] [--out build/game_overlay] [--if-stale]
"""
import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harmonix as hx  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MODS = ROOT / "project" / "mods"
NEW_PART = "gen/main_xbox_10.ark"


def read_entry(hdr, game, path):
    e = hdr.find(path)
    if e.inflated:
        raise ValueError(f"{path} is compressed; not supported")
    part, off = hdr.locate(e)
    with open(game / hdr.part_names[part], "rb") as f:
        f.seek(off)
        return f.read(e.size)


def mod_section(top, name):
    """Returns the nodes of the `(name ...)` array in a mod file."""
    return list(hx.find_prop(top, name)[1:])


# ---------------------------------------------------------------- patches


def patch_main_menu_tree(top):
    """config/gen/main_menu.dtb: quit_to_desktop as the last top-level entry and
    pc_settings at the end of the Options popup."""
    menu = hx.find_prop(top, "proj9_menu")
    if any(n.is_sym("quit_to_desktop") for n in menu):
        raise ValueError("main_menu already patched")
    # main.milo has 7 buttons; the stock top level uses 6 and Options 5.
    menu.append(hx.sym("quit_to_desktop"))
    options = next(n.value for n in menu
                   if n.type == hx.ARRAY and n.value[0].type == hx.ARRAY
                   and n.value[0].value[0].is_sym("options"))
    options.append(hx.sym("pc_settings"))


def patch_main_panel(top, mod):
    """ui/main/gen/main.dtb: new on_select cases and new screens."""
    panel = hx.find_object(top, "UIPanel", "main_panel")
    on_select = hx.find_prop(panel, "on_select")
    switch = next(n.value for n in on_select if n.type == hx.COMMAND and n.value[0].is_sym("switch"))
    switch.extend(mod_section(mod, "on_select_cases"))
    top.extend(mod_section(mod, "objects"))


def patch_venue_detail(top, mod):
    """world/gen/world_objects.dtb: BandCamShot start_shot takes its crowd density/LOD from
    host cvars and can turn off depth of field and self-shadows (venue_detail.dta)."""
    shot_type = next(n.value for n in top
                     if n.type == hx.ARRAY and n.value and n.value[0].is_sym("BandCamShot"))
    found = []

    def walk(arr):
        for n in arr:
            if n.type in (hx.ARRAY, hx.COMMAND, hx.PROPERTY):
                if n.type == hx.ARRAY and n.value and n.value[0].is_sym("start_shot"):
                    found.append(n.value)
                walk(n.value)
    walk(shot_type)
    if len(found) != 1:
        raise ValueError(f"expected one BandCamShot start_shot, found {len(found)}")
    handler = found[0]

    def is_crowd_cmd(n, verb):
        return (n.type == hx.COMMAND and len(n.value) > 1 and n.value[0].type == hx.VAR
                and n.value[0].value == "crowd" and n.value[1].is_sym(verb))

    parents = []

    def find_parent(arr):
        for i, n in enumerate(arr):
            if is_crowd_cmd(n, "set_fullness"):
                parents.append((arr, i))
            if n.type in (hx.ARRAY, hx.COMMAND, hx.PROPERTY):
                find_parent(n.value)
    find_parent(handler)
    if len(parents) != 1:
        raise ValueError(f"expected one crowd set_fullness, found {len(parents)}")
    arr, i = parents[0]
    if not (i + 1 < len(arr) and is_crowd_cmd(arr[i + 1], "set")):
        raise ValueError("crowd 'set lod' does not follow set_fullness")
    arr[i:i + 2] = mod_section(mod, "crowd")
    handler.extend(mod_section(mod, "shot"))


def patch_locale(top, mod):
    top.extend(n for n in mod)


# ---------------------------------------------------------------- build


def inputs():
    return [Path(__file__), Path(hx.__file__)] + sorted(MODS.glob("*.dta"))


def is_stale(out):
    hdr = out / "gen" / "main_xbox.hdr"
    if not hdr.exists():
        return True
    built = hdr.stat().st_mtime
    return any(p.stat().st_mtime > built for p in inputs())


def build(game, out):
    hdr = hx.ArkHeader((game / "gen" / "main_xbox.hdr").read_bytes())
    if len(hdr.part_names) != 10:
        raise ValueError(f"expected 10 ark parts, found {len(hdr.part_names)}")

    def load(path):
        return hx.load_dtb(read_entry(hdr, game, path))

    def mod(name):
        return hx.parse_dta((MODS / name).read_text())

    patched = {}  # archive path -> new .dtb bytes

    seed, top = load("config/gen/main_menu.dtb")
    patch_main_menu_tree(top)
    patched["config/gen/main_menu.dtb"] = hx.dump_dtb(seed, top)

    seed, top = load("ui/main/gen/main.dtb")
    patch_main_panel(top, mod("main_menu.dta"))
    patched["ui/main/gen/main.dtb"] = hx.dump_dtb(seed, top)

    seed, top = load("ui/options/gen/options_console.dtb")
    top.extend(mod_section(mod("pc_settings.dta"), "objects"))
    patched["ui/options/gen/options_console.dtb"] = hx.dump_dtb(seed, top)

    if (MODS / "venue_detail.dta").exists():
        seed, top = load("world/gen/world_objects.dtb")
        patch_venue_detail(top, mod("venue_detail.dta"))
        patched["world/gen/world_objects.dtb"] = hx.dump_dtb(seed, top)

    locale_mod = mod("locale.dta")
    for e in hdr.entries:
        path = hdr.path(e)
        if path.startswith("ui/locale/") and path.endswith("/gen/locale.dtb"):
            seed, top = load(path)
            patch_locale(top, locale_mod)
            patched[path] = hx.dump_dtb(seed, top)

    # New ark part with the patched files, 2 KiB aligned like the original parts.
    part = bytearray()
    placements = {}
    for path, data in patched.items():
        placements[path] = len(part)
        part.extend(data)
        part.extend(b"\0" * (-len(part) % 0x800))
    start = hdr.add_part(NEW_PART, len(part))
    for path, local in placements.items():
        e = hdr.find(path)
        e.offset, e.size = start + local, len(patched[path])

    # Output tree.
    tmp = out.with_name(out.name + ".tmp")
    if tmp.exists():
        shutil.rmtree(tmp)
    (tmp / "gen").mkdir(parents=True)
    for entry in game.iterdir():
        if entry.name != "gen":
            os.symlink(entry.resolve(), tmp / entry.name)
    for entry in (game / "gen").iterdir():
        if entry.name != "main_xbox.hdr":
            os.symlink(entry.resolve(), tmp / "gen" / entry.name)
    (tmp / NEW_PART).write_bytes(part)
    (tmp / "gen" / "main_xbox.hdr").write_bytes(hdr.serialize())
    # The executable checks a SHA-1 of every script it loads; the host replaces the
    # entries of the patched files with these at startup (project/src/game_checksums.cpp).
    (tmp / "checksums.txt").write_text("".join(
        f"./{path} {hashlib.sha1(data).hexdigest()}\n" for path, data in patched.items()))
    if out.exists():
        shutil.rmtree(out)
    tmp.rename(out)
    print(f"build_game_overlay: {len(patched)} scripts patched -> {out}", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", type=Path, default=ROOT / "game")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "game_overlay")
    ap.add_argument("--if-stale", action="store_true",
                    help="do nothing when the overlay is newer than its inputs")
    args = ap.parse_args()
    if args.if_stale and not is_stale(args.out):
        return
    build(args.game.resolve(), args.out)


if __name__ == "__main__":
    main()
