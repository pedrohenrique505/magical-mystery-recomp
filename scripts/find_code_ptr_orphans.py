r"""Lists code addresses that are materialized *in code* (lis rX,hi ; addi/ori rY,rX,lo) and
point at the start of something that looks like a function but that rexglue did not register.
These are function pointers handed to qsort/callbacks/thread starts, which find_orphans.py
(data words only) cannot see. At runtime they show up as
"[FATAL] Call to invalid or unregistered function at guest address 0x...".

A target counts as a candidate when it is 4-aligned, inside an executable block, not a
registered function start, and the word before it is a terminator (blr, bctr, unconditional b)
or zero padding, i.e. it sits where a new function would begin. Switch jump-table bases
(the lis/addi is followed by mtctr) are skipped: they are labels inside a function. Sizes stop at the first
blr/bctr/b, so only trust them for small leaf functions/thunks (the "ok" column).

Usage: find_code_ptr_orphans.py <project_dir> <project_name> <registered.txt> <out.txt>
  registered.txt: same format as for find_orphans.py.
Output lines: "0xTARGET size ok refs=0xFROM[,...]"
"""
import sys
import pyghidra

project_dir, project_name, reg_path, out_path = sys.argv[1:5]
pyghidra.start()

reg = {int(l.strip(), 16) for l in open(reg_path) if l.strip()}


def is_terminator(w):
    if w == 0x4E800020 or w == 0x4E800420:  # blr, bctr
        return True
    return (w >> 26) == 18 and (w & 3) == 0  # b (no link, relative)


with pyghidra.open_program(
    "/home/zathura/Projects/beatles-recomp/game/default.xex",
    project_location=project_dir,
    project_name=project_name,
    analyze=False,
) as flat:
    prog = flat.getCurrentProgram()
    mem = prog.getMemory()
    exec_blocks = [b for b in mem.getBlocks() if b.isExecute() and b.isInitialized()]

    words = {}
    for b in exec_blocks:
        start = b.getStart().getOffset()
        raw = bytearray(b.getSize())
        from jpype import JArray, JByte
        buf = JArray(JByte)(len(raw))
        b.getBytes(b.getStart(), buf)
        raw = bytes((x & 0xFF) for x in buf)
        for i in range(0, len(raw) - 3, 4):
            words[start + i] = int.from_bytes(raw[i:i + 4], "big")
    lo = min(words)
    hi = max(words) + 4
    print("code words:", len(words), hex(lo), hex(hi))

    found = {}
    for a, w in words.items():
        if (w >> 26) != 15 or ((w >> 16) & 31) != 0:  # lis rD,imm == addis rD,0,imm
            continue
        rd = (w >> 21) & 31
        hi16 = w & 0xFFFF
        for k in range(1, 9):
            w2 = words.get(a + 4 * k)
            if w2 is None:
                break
            op = w2 >> 26
            ra = (w2 >> 16) & 31
            rt = (w2 >> 21) & 31
            if op == 14 and ra == rd:  # addi rT,rD,simm
                simm = w2 & 0xFFFF
                simm = simm - 0x10000 if simm & 0x8000 else simm
                tgt = ((hi16 << 16) + simm) & 0xFFFFFFFF
            elif op == 24 and rt == rd:  # ori rA,rS,uimm (rS field is bits 21-25)
                tgt = (hi16 << 16) | (w2 & 0xFFFF)
            else:
                # Stop if rd gets overwritten by something else.
                if ((w2 >> 21) & 31) == rd and op not in (36, 37, 38, 44, 45, 47, 54, 55):
                    break
                continue
            # Switch jump-table base (lis/addi ; add ; mtctr ; bctr): an interior label.
            if any((words.get(a + 4 * (k + j), 0) & 0xFC1FFFFF) == 0x7C0903A6 for j in range(1, 7)):
                break
            if lo <= tgt < hi and tgt % 4 == 0 and tgt not in reg:
                prev = words.get(tgt - 4, 0)
                if prev == 0 or is_terminator(prev):
                    found.setdefault(tgt, []).append(a)
            break

    with open(out_path, "w") as out:
        for tgt in sorted(found):
            size, ok, cur = 0, False, tgt
            for _ in range(256):
                w = words.get(cur)
                if w is None:
                    break
                size += 4
                if is_terminator(w):
                    ok = True
                    break
                cur += 4
            refs = ",".join(hex(r) for r in found[tgt][:4])
            out.write("0x%08X %d %s refs=%s\n" % (tgt, size, ok, refs))
    print("candidates:", len(found))
