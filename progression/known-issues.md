# Problemas conhecidos

## KI-1 — 11 funções com saltos não resolvidos
- **Sintoma:** o `codegen` registra `Analyze: 11 functions still PENDING with unresolved jumps` (nível *debug*, não bloqueia).
- **Causa provável:** jump tables (`switch`) que a análise não resolveu.
- **Risco:** trecho que execute esses saltos pode falhar em tempo de execução.
- **Como investigar:** rodar `rexglue --log-level trace codegen ...`, achar quais funções ficam `PENDING`, ler o código no Ghidra e declarar `[[switch_tables]]`. As opções de manifesto estão em `third_party/rexglue-sdk/src/codegen/config.cpp`.

## KI-2 — `update:\gen\patch_xbox.hdr` não encontrado
- **Sintoma:** `VFS: 'update:\gen' -> [no device]` e `NtCreateFile FAILED ... 0xc000000f` no início de toda execução.
- **Situação:** não bloqueou nada até agora; o jogo segue.
- **Hipótese:** o jogo procura o patch do *title update*; o TU está em `game/$SystemUpdate/`.
- **Ver:** `next-steps.md` §3.

## KI-3 — Funções só alcançadas por vtable sem RTTI
- **Sintoma:** `[FATAL] Call to invalid or unregistered function at guest address 0x...` em tempo de execução.
- **Causa:** o `VTableScanner` do ReXGlue só acha vtables via RTTI do MSVC; este jogo tem muitas classes sem RTTI.
- **Mitigação atual:** `scripts/find_orphans.py` + `scripts/resolve_orphans.py` (155 funções já declaradas). Novos casos podem aparecer em partes do jogo ainda não exercitadas.
- **Limite conhecido:** o tamanho calculado vai até o 1º `blr`/`bctr`/`b`, só confiável para thunks (≤ 32 bytes); funções maiores ficam sem `size` e dependem da análise do SDK.

## KI-4 — Stubs de kernel/áudio
- **Sintoma:** `__imp__XAudioOverrideSpeakerConfig STUB` no log.
- **Impacto:** desconhecido; verificar se há saída de áudio correta.

## KI-5 — Quadro pulado por `async placeholder draw`
- **Sintoma:** `Skipping Vulkan frame presentation due to async placeholder draw usage in this frame` no primeiro quadro.
- **Impacto:** desconhecido; pode ser só o primeiro quadro durante o carregamento de shaders.

## KI-6 — Extensão XEXLoaderWV compilada para outra versão do Ghidra
- **Situação:** o zip do fork é para Ghidra 12.0.4; instalado no 12.1.2 com "Install Anyway" e funciona, mas exigiu habilitar a extensão **Jython** (o loader referencia `org.python.jline`).
- **Plano B:** compilar a partir de `third_party/XEXLoaderWV` com Gradle contra o Ghidra instalado.
