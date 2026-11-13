#!/usr/bin/env bash
# Generates the 10,000-file legal corpus into $1/valid/{wav,flac,mp3}.
# Each file is produced independently by ffmpeg from a synthesized signal,
# varying codec, bit depth, channel count, sample rate and duration.
set -u

ROOT=${1:?usage: gen_valid.sh <corpus-root>}
OUT=$ROOT/valid
mkdir -p "$OUT/wav" "$OUT/flac" "$OUT/mp3"

# Sparse pseudo-random helper: deterministic, no external dependencies.
rand() {
    local seed=$1 mod=$2
    echo $(( (seed * 1103515245 + 12345) % mod ))
}

gen_one() {
    local idx=$1
    local r src sr ch dur extra out kind j level br
    local srates=(8000 11025 16000 22050 32000 44100 48000)
    local brs=(64k 96k 128k 192k 256k 320k)
    r=$(rand $((idx + 7)) 6)
    case $r in
        0) src="sine=frequency=330:sample_rate=SR:duration=DUR" ;;
        1) src="sine=frequency=880:beep_factor=2:sample_rate=SR:duration=DUR" ;;
        2) src="anoisesrc=color=white:amplitude=0.5:sample_rate=SR:duration=DUR" ;;
        3) src="anoisesrc=color=pink:amplitude=0.4:sample_rate=SR:duration=DUR" ;;
        4) src="sine=frequency=120:sample_rate=SR:duration=DUR" ;;
        5) src="anoisesrc=color=brown:amplitude=0.3:sample_rate=SR:duration=DUR" ;;
    esac
    sr=${srates[$(rand $idx 7)]}
    ch=$(( $(rand $((idx + 3)) 2) + 1 ))
    dur=$(( (idx % 5) + 1 )).$(( (idx * 7) % 9 ))
    src=${src//SR/$sr}
    src=${src//DUR/$dur}

    # 4000 WAV across four sample formats.
    if (( idx < 4000 )); then
        kind=$(( idx % 4 ))
        case $kind in
            0) extra="-c:a pcm_s16le"; ext=wav ;;
            1) extra="-c:a pcm_s24le"; ext=wav ;;
            2) extra="-c:a pcm_s32le"; ext=wav ;;
            3) extra="-c:a pcm_f32le"; ext=wav ;;
        esac
        out=$(printf "$OUT/wav/%05d.wav" "$idx")
    # 3000 FLAC across 16/24-bit and compression levels.
    # ffmpeg 8's native FLAC encoder caps at 24-bit; s32 input is truncated to 24.
    elif (( idx < 7000 )); then
        j=$(( idx - 4000 ))
        local depth=$(( j % 3 ))
        level=$(( (j / 3) % 9 ))
        case $depth in
            0) extra="-c:a flac -sample_fmt s16 -compression_level $level" ;;
            1|2) extra="-c:a flac -sample_fmt s32 -bits_per_raw_sample 24 -compression_level $level" ;;
        esac
        out=$(printf "$OUT/flac/%05d.flac" "$j")
    # 3000 MP3 across bitrates, MPEG versions and mono/stereo.
    else
        j=$(( idx - 7000 ))
        br=${brs[$(( j % 6 ))]}
        extra="-c:a libmp3lame -b:a $br -ar $sr -ac $ch"
        out=$(printf "$OUT/mp3/%05d.mp3" "$j")
    fi

    ffmpeg -hide_banner -loglevel error -f lavfi -i "$src" -ac "$ch" $extra -y "$out" </dev/null
}

export -f gen_one rand
export OUT
seq 0 9999 | xargs -P 16 -I{} bash -c 'gen_one "$@"' _ {}

echo "valid counts:"
find "$OUT/wav" -type f | wc -l
find "$OUT/flac" -type f | wc -l
find "$OUT/mp3" -type f | wc -l
