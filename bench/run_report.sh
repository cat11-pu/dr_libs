#!/bin/bash
set -e
FILES="real_l0 real_l5 real_l8"
ITERS=20
printf "%-10s | %-28s | %-10s | %-10s | %-9s | %-8s\n" "素材" "配置 (码率/压缩级)" "best(s)" "avg(s)" "实时倍率" "相对串行"
printf '%.0s-' {1..95}; echo
for f in $FILES; do
  # bitrate
  br=$(ffprobe -v error -show_entries format=bit_rate -of csv=p=0 data/$f.flac)
  brk=$((br/1000))
  lvl=$(echo $f | sed 's/real_l//')
  for m in 0 2 4 8; do
    line=$(./bench_final data/$f.flac $m $ITERS)
    best=$(echo "$line" | sed -n 's/.*best=\([0-9.]*\)s.*/\1/p')
    avg=$(echo "$line" | sed -n 's/.*avg=\([0-9.]*\)s.*/\1/p')
    sp=$(echo "$line" | sed -n 's/.*speed=\([0-9.]*\)x.*/\1/p')
    if [ $m -eq 0 ]; then
      label="串行 (基线)"; rel="1.00x"
    else
      label="并行 ${m} 线程"
      rel=$(echo "$best" | awk -v b="$b0" '{printf "%.2fx", b/$1}')
    fi
    [ $m -eq 0 ] && b0=$best
    cfg="${brk} kbps / L${lvl}"
    name=$f
    printf "%-10s | %-28s | %-10s | %-10s | %8.1fx | %-8s\n" "$name" "$cfg $label" "$best" "$avg" "$sp" "$rel"
  done
  printf '%.0s-' {1..95}; echo
done
