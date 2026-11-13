#!/bin/sh
# Confere os arquivos do test_rec_writer com ffprobe/ffmpeg: duração ~5 s e decodificação sem erro.
set -e
dir="$1"
for f in teste.WAV teste.OPUS teste.MP3; do
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$dir/$f")
  ffmpeg -v error -i "$dir/$f" -f null - 2>"$dir/$f.err"
  if [ -s "$dir/$f.err" ]; then echo "$f: erros ao decodificar"; cat "$dir/$f.err"; exit 1; fi
  echo "$f: $d s"
  awk -v d="$d" 'BEGIN { exit !(d > 4.8 && d < 5.3) }' || { echo "$f: duração fora do esperado"; exit 1; }
done
echo "arquivos: ok"
