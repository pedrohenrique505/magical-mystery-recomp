# Estado atual

_Última atualização: 2026-11-01_

## Resumo

O jogo recompilado **inicia e roda sem abortar** (testado por 60 s). Vulkan (Xenos) e áudio inicializam; a jogabilidade **ainda não foi validada visualmente**.

## Funciona

| Área | Situação |
|---|---|
| Extração da ISO | ✅ `game/default.xex` (10,8 MB) + 4,4 GB de assets |
| Recompilação (`rexglue codegen`) | ✅ ~52,9 mil funções em 130 arquivos C++ (~140 MB), ~2 min, pico ~3 GB |
| Compilação | ✅ binário de ~51 MB, `-O1`, `-j2`, poucos minutos |
| Boot do runtime | ✅ arena de memória, tabela de funções, entrada SDL |
| GPU | ✅ plugin `xenos` sobre Vulkan (AMD RADV Renoir); swapchain 1920×1080; shaders e pipelines criados |
| Áudio | ✅ endpoint inicializa (6 canais, 48 kHz) |
| Ferramentas | ✅ PyGhidra em `.venv`; scripts para achar funções faltantes |

## Não validado / pendente

- O que aparece na janela (tela preta, logos, vídeo, menu) — **precisa de teste visual**.
- Funções com saltos não resolvidos (11) — ver [`known-issues.md`](known-issues.md).
- Dispositivo de *title update* (`update:\`) ausente.
- Instrumentos USB (guitarra, bateria, microfone) — nada implementado ou testado.
- Save/XContent, DLC, conquistas, Live.

## Números úteis

- Manifesto: 155 entradas `orphan_*` + 2 `thunk_*` em `[entrypoint.functions]`.
- Título: ID `454108B1`, XEX versão 0.0.0.1 (sem o TU aplicado), filetime 2009-06-15.
- Ambiente: 8 GB RAM, clang 23, Ghidra 12.1.2, Java 27, Python 3.14.
