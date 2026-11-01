# Próximos passos

Ordem sugerida, do mais barato/informativo para o mais caro.

## 1. Validar o que roda (imediato)
- [ ] Rodar `scripts/run.sh` e registrar o que aparece: tela preta, logos Harmonix/Apple Corps, vídeo, menu, som.
- [ ] Anotar no `log.md` o resultado e, se houver travamento, o último `[FATAL]` do `build/run.log`.

## 2. Estabilidade do código recompilado
- [ ] Tratar as 11 funções `PENDING` com saltos não resolvidos (provável `switch` / jump tables). Descobrir quais são (ver `known-issues.md`) e declarar `[[switch_tables]]` no manifesto.
- [ ] Rodar cenários mais longos (menus, carregar uma música) e repetir o laço de funções faltantes (`scripts/resolve_orphans.py`) quando aparecer `unregistered function`.
- [ ] Decidir se vale gerar o relatório de *vtables* completo para antecipar funções faltantes, em vez de esperar cada crash.

## 3. Title update
- [ ] O jogo procura `update:\gen\patch_xbox.hdr`. Investigar se o TU em `game/$SystemUpdate/su20076000_00000000` é um XEXP que deveria ser aplicado ao `default.xex` (o XEXLoaderWV suporta XEXP) e se muda o código.
- [ ] Se sim, recompilar a partir do XEX com o TU aplicado e refazer o manifesto.

## 4. Áudio e vídeo
- [ ] Confirmar decodificação de XMA (música e SFX) e latência.
- [ ] Verificar a mensagem `Skipping Vulkan frame presentation due to async placeholder draw usage` e se afeta a imagem.
- [ ] Vídeos Bink (blocos `BINK*` no XEX).

## 5. Instrumentos (diferencial deste jogo)
- [ ] Mapear como o jogo lê guitarra/baixo/bateria/microfone (HID USB via XInput/`XamInput*`/`XUsb*`?) e o que o runtime do ReXGlue já expõe.
- [ ] Mapeamento de teclado/gamepad como solução mínima jogável.
- [ ] Captura de microfone para o modo vocal (3 vozes).

## 6. Persistência e extras
- [ ] Saves (XContent/STFS), perfil e conquistas.
- [ ] DLC / músicas extras (STFS em `Content/`).

## 7. Qualidade do projeto
- [ ] Inicializar o git e fazer o primeiro commit (o repositório ainda não é git).
- [ ] Documentar, na wiki do ReXGlue ou aqui, quirks do manifesto descobertos (ver `CLAUDE.md`).
- [ ] Avaliar contribuir correções/relatos de volta ao ReXGlue (vtables sem RTTI).
