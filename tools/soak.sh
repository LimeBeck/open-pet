#!/usr/bin/env bash
# Soak-тест (§13, п. 9): длительная фоновая работа с замерами.
#
# Смысл не в том, чтобы «подержать приложение открытым», а в том, чтобы
# поймать то, что не видно за минуту: утечку памяти, расползание числа
# перерисовок, накопление дескрипторов, залипание в состоянии.
#
#   ./tools/soak.sh            # 8 часов, как требует §13
#   ./tools/soak.sh 600        # 10 минут, для проверки самой оснастки
#
# Приложение остаётся видимым и реагирует на настоящие события: soak
# на скрытом окне ничего не докажет — именно отрисовка и стоит дороже всего.

set -euo pipefail

cd "$(dirname "$0")/.."

DURATION=${1:-28800}
INTERVAL=${SOAK_INTERVAL:-60}
BINARY=build/apps/desktop-kde/open-pet
OUT=${SOAK_OUT:-soak-$(date +%Y%m%d-%H%M%S)}

[ -x "$BINARY" ] || { echo "нет сборки: $BINARY" >&2; exit 1; }

mkdir -p "$OUT"
echo "soak: ${DURATION} с, замер каждые ${INTERVAL} с, вывод в $OUT/"

QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES='openpet.*=true' \
    "$BINARY" > "$OUT/app.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT

sleep 5
kill -0 $PID 2>/dev/null || { echo "приложение не запустилось, см. $OUT/app.log" >&2; exit 1; }

echo "секунды,private_dirty_kib,rss_kib,cpu_percent,потоки,дескрипторы" > "$OUT/metrics.csv"

prev_ticks=0
started=$(date +%s)
tick_hz=$(getconf CLK_TCK)

while true; do
    now=$(date +%s)
    elapsed=$((now - started))
    [ "$elapsed" -ge "$DURATION" ] && break
    kill -0 $PID 2>/dev/null || { echo "процесс умер на $elapsed с" >&2; break; }

    # Private_Dirty, а не RSS: в GPU-ускоренном процессе RSS измеряет
    # графический стек системы, а не приложение (ADR-006).
    pd=$(awk '/Private_Dirty/ {sum+=$2} END {print sum+0}' "/proc/$PID/smaps" 2>/dev/null || echo 0)
    rss=$(awk '/VmRSS/ {print $2+0}' "/proc/$PID/status" 2>/dev/null || echo 0)
    threads=$(awk '/Threads/ {print $2+0}' "/proc/$PID/status" 2>/dev/null || echo 0)
    fds=$(ls "/proc/$PID/fd" 2>/dev/null | wc -l)

    read -r utime stime < <(awk '{print $14, $15}' "/proc/$PID/stat" 2>/dev/null || echo "0 0")
    ticks=$((utime + stime))
    if [ "$prev_ticks" -gt 0 ]; then
        cpu=$(awk -v d=$((ticks - prev_ticks)) -v i="$INTERVAL" -v hz="$tick_hz" \
            'BEGIN {printf "%.2f", d / hz / i * 100}')
    else
        cpu="0.00"
    fi
    prev_ticks=$ticks

    echo "$elapsed,$pd,$rss,$cpu,$threads,$fds" >> "$OUT/metrics.csv"
    sleep "$INTERVAL"
done

kill $PID 2>/dev/null || true
wait $PID 2>/dev/null || true
trap - EXIT

echo
echo "=== итог ==="
python3 - "$OUT/metrics.csv" <<'PY'
import csv, sys

rows = list(csv.DictReader(open(sys.argv[1])))
if len(rows) < 2:
    print("замеров слишком мало для выводов")
    sys.exit(0)

def col(name):
    return [float(r[name]) for r in rows]

pd, cpu, threads, fds = col("private_dirty_kib"), col("cpu_percent"), col("потоки"), col("дескрипторы")
hours = float(rows[-1]["секунды"]) / 3600 or 1e-9

# Утечка ищется по наклону, а не по разнице краёв: единичный всплеск
# в конце прогона выглядел бы как утечка, а плавный рост — нет.
n = len(pd)
mean_x = (n - 1) / 2
mean_y = sum(pd) / n
denom = sum((i - mean_x) ** 2 for i in range(n)) or 1e-9
slope = sum((i - mean_x) * (pd[i] - mean_y) for i in range(n)) / denom
per_hour = slope * (n - 1) / hours / 1024

print(f"замеров: {n}, длительность: {hours:.2f} ч")
print(f"Private_Dirty: {pd[0]/1024:.1f} -> {pd[-1]/1024:.1f} МиБ, "
      f"максимум {max(pd)/1024:.1f}")
print(f"наклон: {per_hour:+.2f} МиБ/ч")
print(f"CPU: среднее {sum(cpu[1:])/max(1,len(cpu)-1):.2f}%, максимум {max(cpu):.2f}%")
print(f"потоки: {int(threads[0])} -> {int(threads[-1])}, "
      f"дескрипторы: {int(fds[0])} -> {int(fds[-1])}")

print()
verdicts = []
if per_hour > 1.0:
    verdicts.append(f"ПОДОЗРЕНИЕ НА УТЕЧКУ: +{per_hour:.2f} МиБ/ч")
if max(pd) / 1024 > 60:
    verdicts.append(f"память превысила цель §7: {max(pd)/1024:.1f} > 60 МиБ")
if sum(cpu[1:]) / max(1, len(cpu) - 1) > 2.0:
    verdicts.append("CPU выше цели §7 в 2%")
if fds[-1] > fds[0] * 1.5:
    verdicts.append("дескрипторы растут")

print("\n".join(verdicts) if verdicts else "отклонений не найдено")
PY
