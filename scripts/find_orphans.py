"""Lista palavras de 32 bits (big-endian) em dados que apontam para codigo
nao coberto por nenhuma funcao (Ghidra ou rexglue).

Uso: find_orphans.py <project_dir> <project_name> <registered.txt> <out.txt>
"""
import sys
import pyghidra

project_dir, project_name, reg_path, out_path = sys.argv[1:5]
pyghidra.start()

from jpype import JArray, JByte
from ghidra.program.model.address import AddressSet
from ghidra.app.cmd.disassemble import DisassembleCommand

reg = {int(l.strip(), 16) for l in open(reg_path) if l.strip()}

with pyghidra.open_program(
    "/home/zathura/Projects/beatles-recomp/game/default.xex",
    project_location=project_dir,
    project_name=project_name,
    analyze=False,
) as flat:
    prog = flat.getCurrentProgram()
    mem = prog.getMemory()
    fm = prog.getFunctionManager()
    listing = prog.getListing()
    space = prog.getAddressFactory().getDefaultAddressSpace()
    addr = lambda v: space.getAddress(v)

    exec_blocks = [b for b in mem.getBlocks() if b.isExecute()]
    data_blocks = [b for b in mem.getBlocks() if b.isInitialized() and not b.isExecute() and b.getName() not in ('.reloc', '.idata')]
    for b in mem.getBlocks():
        print("block", b.getName(), b.getStart(), b.getEnd(), "x" if b.isExecute() else "-", "init" if b.isInitialized() else "uninit")
    lo = min(b.getStart().getOffset() for b in exec_blocks)
    hi = max(b.getEnd().getOffset() for b in exec_blocks)

    targets = {}
    for b in data_blocks:
        start = b.getStart().getOffset()
        size = int(b.getEnd().getOffset() - start + 1)
        arr = JArray(JByte)(size)
        b.getBytes(b.getStart(), arr)
        data = bytes(memoryview(arr).cast("B")) if hasattr(arr, "__buffer__") or True else None
        for i in range(0, size - 3, 4):
            v = int.from_bytes(data[i:i + 4], "big")
            if v < lo or v > hi or v & 3 or v in reg:
                continue
            if fm.getFunctionContaining(addr(v)) is not None:
                continue
            targets.setdefault(v, []).append(start + i)

    print("seed candidates:", len(targets))

    def analyze(v):
        cur = addr(v)
        size, ok, note, tail = 0, False, "", None
        for _ in range(128):
            ins = listing.getInstructionAt(cur)
            if ins is None:
                DisassembleCommand(AddressSet(cur, cur.add(3)), None, True).applyTo(prog)
                ins = listing.getInstructionAt(cur)
            if ins is None:
                note = "undisassemblable"
                break
            size += ins.getLength()
            m = ins.getMnemonicString()
            if m in ("blr", "bctr", "b"):
                ok = True
                if m == "b":
                    fl = ins.getFlows()
                    if fl:
                        tail = fl[0].getOffset()
                break
            cur = cur.add(ins.getLength())
        if not ok and not note:
            note = "no-terminal"
        return size, ok, note, tail

    results = {}
    work = sorted(targets)
    while work:
        v = work.pop()
        if v in results:
            continue
        size, ok, note, tail = analyze(v)
        results[v] = (size, ok, note)
        # fecho transitivo: `b` final para alvo sem funcao -> tambem e orfao
        if tail is not None and lo <= tail <= hi and not tail & 3 and tail not in reg \
                and tail not in results and fm.getFunctionContaining(addr(tail)) is None:
            targets.setdefault(tail, [])
            work.append(tail)
    print("candidates after tail-closure:", len(results))

    with open(out_path, "w") as out:
        for v in sorted(results):
            size, ok, note = results[v]
            refs = targets[v]
            out.write("0x%08X size=%d ok=%s refs=%d first=%s %s\n" % (
                v, size, ok, len(refs), ("0x%08X" % refs[0]) if refs else "tail", note))
    print("done")
