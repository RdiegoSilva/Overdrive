# REIS BOT

Plataforma de ação estilo Mega Man para PS2 (homebrew, PS2SDK + gsKit, em C).
Sem texturas nem áudio: tudo é desenhado com retângulos, então não precisa de assets.

## Compilar

```sh
make        # gera reisbot.elf (precisa de PS2SDK e gsKit instalados)
make clean
```

## Controles

| Botão | Ação |
|---|---|
| D-pad ← → | Andar |
| X | Pular (segure = pulo mais alto) |
| Quadrado | Atirar (segure ~1 s e solte = tiro carregado, dano 3) |
| Start | Começar / pausar |

## O que tem

- Fase com rolagem lateral, buracos, espinhos, escada e plataformas
- Inimigos: **Met** (fica invulnerável sob o capacete), **Batton** (voa em onda) e um **chefe** que pula e atira em leque
- Barra de vida vertical, itens de cura, 3 vidas, checkpoints e porta do chefe
- Tela de título, READY, pausa, game over e vitória

## Ideias para evoluir

- Escorregar (baixo + X) e subir escadas
- Trocar armas ao derrotar o chefe (mecânica clássica)
- Sprites com texturas (gsKit_prim_sprite_texture) e música via audsrv
- Mais fases e chefes, salvando progresso no memory card
