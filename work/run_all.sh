#!/usr/bin/env bash
# End-to-end reproduction of the decoder comparison.
# Assumes the reference-library prefix from the deb extraction exists
# (see env.sh) and ffmpeg + gcc are on PATH.
set -euo pipefail

cd "$(dirname "$0")"
source ./env.sh

REPO=/home/cat/gsb/uid3557
LIBDIR=$PREFIX/lib/x86_64-linux-gnu

echo "== 1. build dr_libs_test against libsndfile/libFLAC/libmpg123"
gcc -O2 -Wall -Wextra -Wpedantic \
    -I$PREFIX/include -I$PREFIX/include/x86_64-linux-gnu \
    $REPO/tests/dr_libs_test.c -o build/dr_libs_test \
    -L$LIBDIR -Wl,-rpath,$LIBDIR \
    -lsndfile -lFLAC -lmpg123 -lvorbisenc -lvorbis -logg -lopus \
    /usr/lib/x86_64-linux-gnu/libmp3lame.so.0 \
    /usr/lib/x86_64-linux-gnu/libtwolame.so.0 -ldl -lpthread -lm

echo "== 2. generate corpora"
bash gen_valid.sh corpus
python3 gen_malformed.py corpus

echo "== 3. valid-corpus sample comparison (decoding + seeking)"
echo '# empty exceptions' > empty_exceptions.txt
./build/dr_libs_test --test decoding --exceptions empty_exceptions.txt \
    corpus/valid/wav corpus/valid/flac corpus/valid/mp3 \
    > report/valid_stdout.log 2> report/valid_stderr.log
./build/dr_libs_test --test seeking --exceptions empty_exceptions.txt \
    corpus/valid/wav corpus/valid/flac corpus/valid/mp3 \
    > report/valid_seek_stdout.log 2> report/valid_seek_stderr.log

echo "== 4. malformed-corpus behavior comparison"
gcc -O2 -Wall -Wextra -I$REPO -I$PREFIX/include -I$PREFIX/include/x86_64-linux-gnu \
    malformed_probe.c -o build/malformed_probe \
    -L$LIBDIR -Wl,-rpath,$LIBDIR -lFLAC -lmpg123 -lsndfile -lvorbisenc -lvorbis \
    -logg -lopus /usr/lib/x86_64-linux-gnu/libmp3lame.so.0 \
    /usr/lib/x86_64-linux-gnu/libtwolame.so.0 -ldl -lpthread -lm
find corpus/malformed -type f | sort > report/malformed_list.txt
./build/malformed_probe $(cat report/malformed_list.txt) \
    > report/malformed_raw.ssv 2> report/malformed_probe_stderr.log
python3 analyze_malformed.py report/malformed_raw.ssv report
python3 categorize_malformed.py

echo "== 5. memory stability"
gcc -O2 -Wall -Wextra -I$REPO mem_loop.c -o build/mem_loop
gcc -O1 -g -fsanitize=address,undefined -Wall -Wextra -I$REPO \
    mem_loop.c -o build/mem_loop_asan
./build/mem_loop 100000 \
    corpus/valid/wav/00000.wav corpus/valid/flac/00000.flac corpus/valid/mp3/00000.mp3
ASAN_OPTIONS=detect_leaks=1 ./build/mem_loop_asan 10000 \
    corpus/valid/wav/00000.wav corpus/valid/flac/00000.flac corpus/valid/mp3/00000.mp3

echo "all stages complete; see report/ for artifacts."
