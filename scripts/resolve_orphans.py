"""Laco automatico: roda `rexglue codegen`, le os alvos "target not in any function",
calcula o tamanho de cada um no Ghidra (size_of.py) e os acrescenta a
[entrypoint.functions] do manifesto. Repete ate o codegen passar (max 8 rodadas).

Uso: resolve_orphans.py <workdir>
  <workdir>/ghproj deve conter uma COPIA do projeto Ghidra (beatles-rock-band.gpr/.rep),
  para nao conflitar com o Ghidra aberto. Requer .venv com pyghidra.
"""
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
W = sys.argv[1]
man = f"{ROOT}/project/beatles_manifest.toml"
env = dict(os.environ, GHIDRA_INSTALL_DIR="/opt/ghidra")
ERR = r"\n\s+0x([0-9A-F]{8}) from 0x[0-9A-F]{8}: bl? 0x[0-9A-F]{8} from 0x[0-9A-F]{8} - target not in any function"

for rnd in range(1, 9):
    r = subprocess.run(["nice", "-n", "10", f"{ROOT}/tools/rexglue/linux-amd64/bin/rexglue",
                        "--log-file", f"{ROOT}/build/codegen.log", "codegen", man],
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    errs = sorted(set(re.findall(ERR, out)))
    print(f"round {rnd}: rc={r.returncode} unresolved={errs}", flush=True)
    if r.returncode == 0 or not errs:
        break
    open(f"{W}/addrs.txt", "w").write("\n".join(errs) + "\n")
    subprocess.run([f"{ROOT}/.venv/bin/python", "-I", f"{ROOT}/scripts/size_of.py",
                    f"{W}/ghproj", "beatles-rock-band", f"{W}/addrs.txt", f"{W}/sized.txt"],
                   env=env, capture_output=True)
    lines = []
    for l in open(f"{W}/sized.txt"):
        a, s, _ = l.split()
        s = int(s)
        lines.append(f'0x{a} = {{ name = "orphan_{a}", size = {s} }}' if s <= 32
                     else f'0x{a} = {{ name = "orphan_{a}" }}')
    open(man, "a").write("\n".join(lines) + "\n")
    print("  added:", len(lines), flush=True)
