# Diário

## 2026-11-01 — Do zero ao jogo rodando

- **Decisão:** recompilação estática (não decomp matching). Usar o **ReXGlue SDK pré-compilado** em vez de compilar o SDK (8 GB de RAM). XenonRecomp/XenosRecomp/rb3 ficam só como referência.
- ISO extraída com `extract-xiso`; `default.xex` (título `454108B1`).
- `rexglue init` + `codegen` na primeira tentativa falhou com 1 erro: `b 0x8264E3F0` para um thunk de 4 bytes (`b 0x8264E060`) que a análise não reconhecia.
  - Declarar a função **sem `size`** não resolveu; o motivo real era que `[functions]` na raiz do manifesto é **ignorado em silêncio**. O correto é `[entrypoint.functions]`.
- Instalado o loader XEXLoaderWV no Ghidra (aviso de versão; precisou habilitar Jython). Usado o Ghidra para desmontar o thunk.
- Compilação: `clang` + `mold` + `ccache`, `-O1`, `-j2`; os presets pedem `clang-20`, que não existe na máquina.
- Primeira execução: sem GPU (`gpu_plugin not set`). Corrigido com `GPU_PLUGINS xenos` no CMake **e** `--gpu_plugin xenos` na linha de comando.
- Crashes sucessivos `Call to invalid or unregistered function` (`0x82648980`, `0x828807E8`): thunks de vtable sem RTTI. Em vez de corrigir um por rodada, instalado **PyGhidra** (já embutido no Ghidra, wheels em `Features/PyGhidra/pypkg/dist`) e escrito `scripts/find_orphans.py`, que lista alvos de dados sem função registrada (252 candidatos).
  - Filtrar candidatos dentro do intervalo de outro (rótulos internos) e seguir cadeias de `b` (thunks que saltam para outro stub).
  - O Ghidra **não** é bom oráculo para o que o rexglue cobre (corpos não contíguos); o validador do `codegen` é. Daí `scripts/resolve_orphans.py`.
- Resultado: 155 funções adicionadas, `codegen` limpo, jogo roda 60 s sem abortar.
- Criados `CLAUDE.md`, `README.md` e esta pasta.
