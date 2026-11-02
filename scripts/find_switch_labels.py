r"""Decodes MSVC PPC switch jump tables and lists every case label.

For each `bctr`, scans up to 24 instructions back, propagating lis/addi/ori constants, and
recognizes:
  lwzx  rX,rT,rI ; mtctr rX ; bctr                         (absolute 32-bit targets)
  lhzx  r0,rT,rI ; [rlwinm r0,r0,sh] ; add rC,rB,r0 ; mtctr  (base + 16-bit offset)
  lbzx  r0,rT,rI ; [rlwinm r0,r0,sh] ; add rC,rB,r0 ; mtctr  (base + 8-bit offset << sh)
The entry count comes from the last `cmplwi crN,rIdx,N` (count = N+1) before the load.

Why: when rexglue cannot resolve a table, the case bodies look like code nobody calls and
resolve_orphans.py ends up declaring them as functions, which splits the switch (see KI-8).
Use the output to drop such entries from the manifest and to write [[switch_tables]].

Usage: find_switch_labels.py <project_dir> <project_name> <out.txt>
Output lines: "bctr=0xADDR func_hint=0xLISADDR type=lhzx idx=rN count=N labels=0x..,0x.."
"""
import sys
import pyghidra

project_dir, project_name, out_path = sys.argv[1:4]
pyghidra.start()

with pyghidra.open_program(
    "/home/zathura/Projects/beatles-recomp/game/default.xex",
    project_location=project_dir,
    project_name=project_name,
    analyze=False,
) as flat:
    from jpype import JArray, JByte

    prog = flat.getCurrentProgram()
    mem = prog.getMemory()
    IMG_BASE, IMG_SIZE = 0x82000000, 0xB90000
    image = bytearray(IMG_SIZE)  # flat copy of the loaded image
    code_ranges = []
    for b in mem.getBlocks():
        start, size = b.getStart().getOffset(), int(b.getSize())
        if not b.isInitialized() or not (IMG_BASE <= start < IMG_BASE + IMG_SIZE):
            continue
        size = min(size, IMG_BASE + IMG_SIZE - start)
        buf = JArray(JByte)(size)
        b.getBytes(b.getStart(), buf)
        image[start - IMG_BASE:start - IMG_BASE + size] = bytes(buf)
        if b.isExecute():
            code_ranges.append((start, start + size))

    def read(addr, n):
        off = addr - IMG_BASE
        if off < 0 or off + n > IMG_SIZE:
            return None
        return int.from_bytes(image[off:off + n], "big")

    def in_code(a):
        return any(lo <= a < hi for lo, hi in code_ranges)

    results = []
    for lo, hi in code_ranges:
        for a in range(lo, hi, 4):
            if read(a, 4) != 0x4E800420:  # bctr
                continue
            window = [a - 4 * k for k in range(24, 0, -1) if a - 4 * k >= lo]
            consts = {}
            ctr_reg = None
            load = None  # (kind, table_reg, index_reg, dest_reg)
            shift = 0
            base_reg = None
            bound = None
            lis_addr = None
            for ia in window:
                w = read(ia, 4)
                op = w >> 26
                rt = (w >> 21) & 31
                ra = (w >> 16) & 31
                rb = (w >> 11) & 31
                if op == 15 and ra == 0:  # lis
                    consts[rt] = (w & 0xFFFF) << 16
                    lis_addr = lis_addr or ia
                elif op == 14 and ra in consts:  # addi
                    simm = w & 0xFFFF
                    simm = simm - 0x10000 if simm & 0x8000 else simm
                    consts[rt] = (consts[ra] + simm) & 0xFFFFFFFF
                elif op == 24 and rt in consts and ra == rt:  # ori rX,rX,imm (nop when imm=0)
                    consts[ra] = consts[rt] | (w & 0xFFFF)
                elif op == 10 and ((w >> 21) & 1) == 0:  # cmplwi crN,rA,uimm
                    bound = (ra, w & 0xFFFF)
                elif op == 31:
                    xo = (w >> 1) & 0x3FF
                    if xo in (23, 279, 87):  # lwzx, lhzx, lbzx
                        kind = {23: "lwzx", 279: "lhzx", 87: "lbzx"}[xo]
                        # Capture the table address now: the register is often reused for the base.
                        load = (kind, consts.get(ra), rb, rt)
                        shift = 0
                        consts.pop(rt, None)
                    elif xo == 266 and load:  # add rT,rA,rB
                        other = ra if rb == load[3] else rb if ra == load[3] else None
                        if other is not None and other in consts:
                            base_reg = consts[other]
                            ctr_reg = rt
                    elif w & 0xFC1FFFFF == 0x7C0903A6:  # mtctr
                        ctr_reg = rt
                    else:
                        consts.pop(ra if xo in (24, 28, 444, 536, 792, 824) else rt, None)
                elif op == 21 and load and ((w >> 21) & 31) == load[3]:  # rlwinm on loaded value
                    shift = (w >> 11) & 31
                elif op in (32, 34, 40, 42):  # loads clobber rt
                    consts.pop(rt, None)
            if not load or bound is None:
                continue
            kind, table, ireg, _ = load
            if table is None:
                continue
            count = bound[1] + 1
            if count > 1024:
                continue
            labels = []
            for i in range(count):
                if kind == "lwzx":
                    t = read(table + 4 * i, 4)
                elif kind == "lhzx":
                    v = read(table + 2 * i, 2)
                    t = None if v is None or base_reg is None else (base_reg + (v << shift)) & 0xFFFFFFFF
                else:
                    v = read(table + i, 1)
                    t = None if v is None or base_reg is None else (base_reg + (v << shift)) & 0xFFFFFFFF
                if t is None or not in_code(t):
                    labels = None
                    break
                labels.append(t)
            if labels:
                results.append((a, lis_addr, kind, bound[0], count, labels))

    with open(out_path, "w") as out:
        for a, lis_addr, kind, idx, count, labels in results:
            out.write("bctr=0x%08X func_hint=0x%08X type=%s idx=r%d count=%d labels=%s\n" % (
                a, lis_addr or 0, kind, idx, count, ",".join("0x%08X" % t for t in labels)))
    print("tables:", len(results))
