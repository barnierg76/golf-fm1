#!/bin/zsh
# Golf voor de FM-1 bouwen met fm1-base.
#   ./bouw.sh host      de simulator op de Mac (en de tests in tests/)
#   ./bouw.sh device    de firmware: build/golf.fwsc
# fm1-base staat in $FM1_BASE (standaard ~/Desktop/fm1/ref/fm1-base). De map van dit project heeft een spatie in haar
# pad, wat de bouwscripts niet verdragen; daarom wordt de app eerst naar $FM1_BASE/golf gekopieerd.
set -e
HERE=${0:A:h}
BASE=${FM1_BASE:-$HOME/Desktop/fm1/ref/fm1-base}
PY=${FM1_PY:-$HOME/Desktop/fm1/venv/bin/python}
export JIELI_TOOLCHAIN=${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain} AC79_SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
rsync -a --delete --exclude build --exclude '.git*' "$HERE/" "$BASE/golf/"
mkdir -p "$BASE/golf/build" "$HERE/build"
cd "$BASE"
if [[ $1 == host ]]; then
    $PY tools/build.py golf --host
    for t in golf/tests/*.txt; do
        out="$HERE/build/host/${t:t:r}"
        mkdir -p "$out"
        echo "== ${t:t}"
        golf/build/host/golf_host "$t" "$out"
    done
else
    $PY tools/build.py golf ${@:2}
    cp "$(ls -t golf/build/*.fwsc | head -1)" "$HERE/build/golf.fwsc"
    ls -l "$HERE/build/golf.fwsc"
fi
