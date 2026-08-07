#!/usr/bin/env bash
# Локальный прогон того же, что проверяет CI.
#
# Существует потому, что расхождение уже случилось: clippy в CI запускается
# с -D warnings, локально не запускался вовсе, и сборка падала на каждом
# пуше, пока это не заметили. Проверка, которую легко забыть, рано или
# поздно забывается — поэтому она здесь одной командой.

set -euo pipefail

cd "$(dirname "$0")/.."

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }

step "Форматирование (Rust)"
cargo fmt --manifest-path core/Cargo.toml --check

step "Clippy — предупреждения считаются ошибками, как в CI"
cargo clippy --manifest-path core/Cargo.toml --all-targets -- -D warnings

step "Тесты ядра"
cargo test --manifest-path core/Cargo.toml

step "Сборка хоста"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build build

step "Установка в стейджинг"
rm -rf build/stage
cmake --install build --prefix "$PWD/build/stage/usr" >/dev/null
find build/stage -type f | sed 's|^build/stage||'

step "Ссылки в документации"
broken=0
while IFS= read -r file; do
    dir=$(dirname "$file")
    while IFS= read -r link; do
        [ -e "$dir/$link" ] || { echo "битая ссылка: $file -> $link"; broken=1; }
    done < <(grep -oE '\]\([^)]+\)' "$file" \
        | sed -E 's/^\]\(//; s/\)$//' \
        | grep -v '^http' | sed 's/#.*//' | grep -v '^$')
done < <(find . -name '*.md' -not -path './.git/*' -not -path './build/*' -not -path './core/target/*')
[ "$broken" -eq 0 ] || exit 1
echo "битых ссылок нет"

printf '\n\033[1;32mВсё прошло.\033[0m\n'
