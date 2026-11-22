#!/usr/bin/env bash
# Compila o REIS BOT (PS2SDK + gsKit) e gera reisbot.elf
#
# Uso:
#   ./build.sh          compila
#   ./build.sh clean    limpa os arquivos gerados
#   ./build.sh rebuild  limpa e compila de novo
#   ./build.sh run      compila e abre no PCSX2 (se estiver no PATH)

set -euo pipefail
cd "$(dirname "$0")"

# Valores padrao comuns (imagem Docker ps2dev e instalacao em /usr/local/ps2dev)
if [ -z "${PS2DEV:-}" ] && [ -d /usr/local/ps2dev ]; then
    export PS2DEV=/usr/local/ps2dev
fi
if [ -z "${PS2SDK:-}" ] && [ -n "${PS2DEV:-}" ] && [ -d "$PS2DEV/ps2sdk" ]; then
    export PS2SDK="$PS2DEV/ps2sdk"
fi
if [ -n "${PS2DEV:-}" ]; then
    export PATH="$PATH:$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:${PS2SDK:-}/bin"
fi

fail() { echo "ERRO: $*" >&2; exit 1; }

[ -n "${PS2DEV:-}" ] || fail "PS2DEV nao definido. Exemplo: export PS2DEV=/usr/local/ps2dev"
[ -n "${PS2SDK:-}" ] || fail "PS2SDK nao definido. Exemplo: export PS2SDK=\$PS2DEV/ps2sdk"
[ -d "$PS2SDK" ]     || fail "PS2SDK nao encontrado em $PS2SDK"
command -v ee-gcc >/dev/null 2>&1 || fail "ee-gcc nao encontrado no PATH (toolchain ps2dev nao instalada?)"
[ -d "$PS2DEV/gsKit" ] || fail "gsKit nao encontrado em $PS2DEV/gsKit"

JOBS="$(nproc 2>/dev/null || echo 2)"
ELF="reisbot.elf"

case "${1:-build}" in
    clean)
        make clean
        ;;
    rebuild)
        make clean
        make -j"$JOBS"
        ;;
    run)
        make -j"$JOBS"
        command -v pcsx2 >/dev/null 2>&1 || fail "pcsx2 nao encontrado no PATH"
        exec pcsx2 -elf "$PWD/$ELF"
        ;;
    build)
        make -j"$JOBS"
        ;;
    *)
        fail "opcao desconhecida: $1 (use build, clean, rebuild ou run)"
        ;;
esac

if [ "${1:-build}" != "clean" ]; then
    [ -f "$ELF" ] || fail "o build terminou mas $ELF nao foi gerado"
    echo "OK: $ELF gerado ($(du -h "$ELF" | cut -f1))"
fi
