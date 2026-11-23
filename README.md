# REIS BOT

Plataforma de ação 2D para PS2, com um garoto adolescente de boné e moletom como herói (homebrew, PS2SDK + gsKit, em C).
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
| → → ou ← ← (2x rápido) | Dash (só depois de derrotar o Capitão Lobo; permanente; funciona segurando Quadrado) |
| Quadrado | Atirar (segure ~1 s e solte = tiro carregado, dano 3) |
| Start | Começar / pausar |

## Menu

Tela de título com **Iniciar**, **Senha**, **Opções** e **Créditos** (cima/baixo escolhe, X ou Start confirma, Círculo volta).
Opções: volume geral, música, efeitos (guardados, prontos para ligar ao áudio), tremor de tela e vinheta (ligar/desligar).

## Senhas

A senha de 5 letras/números aparece na **pausa** e no **Game Over**. Ela guarda o checkpoint, as vidas (1 a 8) e se você já tem o dash (Capitão Lobo derrotado). Digite em **Senha** no menu: cima/baixo muda o caractere, esquerda/direita troca a posição, X ou Start confirma. O score não é guardado.

## O que tem

- **Parte 1 ("A Rua")**: fase com rolagem lateral, buracos, espinhos, escada e plataformas, terminando numa porta de saída
- Inimigos: **Tatu bola** (enrola na bola e fica invulnerável) e **Batton** (voa em onda). O código do **chefe** (pula e atira em leque) já existe e está reservado para a Parte 2
- Barra de vida vertical, itens de cura, 3 vidas e checkpoints
- Tela de título, abertura da Parte 1, READY, pausa, game over e conclusão da Parte 1

## Ideias para evoluir

- **Parte 2**: sequência de chefes (o chefe atual já está no código)
- Escorregar (baixo + X) e subir escadas
- Trocar armas ao derrotar o chefe (mecânica clássica)
- Sprites com texturas (gsKit_prim_sprite_texture) e música via audsrv
- Mais fases e chefes, salvando progresso no memory card
