"""Calcula o tamanho (ate o 1o blr/bctr/b) de cada endereco listado em <addrs.txt>.
Uso: size_of.py <project_dir> <project_name> <addrs.txt> <out.txt>   (saida: "0xADDR size")"""
import sys, pyghidra
pyghidra.start()
from ghidra.program.model.address import AddressSet
from ghidra.app.cmd.disassemble import DisassembleCommand
pd, pn, inp, outp = sys.argv[1:5]
with pyghidra.open_program("/home/zathura/Projects/beatles-recomp/game/default.xex", project_location=pd, project_name=pn, analyze=False) as flat:
    prog = flat.getCurrentProgram(); sp = prog.getAddressFactory().getDefaultAddressSpace(); L = prog.getListing()
    with open(outp, "w") as out:
        for line in open(inp):
            line = line.strip()
            if not line: continue
            cur = sp.getAddress(int(line, 16)); size = 0; ok = False
            for _ in range(128):
                ins = L.getInstructionAt(cur)
                if ins is None:
                    DisassembleCommand(AddressSet(cur, cur.add(3)), None, True).applyTo(prog); ins = L.getInstructionAt(cur)
                if ins is None: break
                size += ins.getLength()
                if ins.getMnemonicString() in ("blr", "bctr", "b"): ok = True; break
                cur = cur.add(ins.getLength())
            out.write("%s %d %s\n" % (line, size, ok))
