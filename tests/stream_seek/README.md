# Streaming decode with sample-exact time seeking

`stream/dr_stream_seek.h` wraps `dr_wav`, `dr_flac` and `dr_mp3` behind one
interface that decodes in fixed-size chunks (streaming) and can jump directly
to the PCM frame nearest a requested time, producing samples identical to a
from-scratch linear decode.

## Interface

All functions are declared in `stream/dr_stream_seek.h`. Define
`DR_STREAM_SEEK_IMPLEMENTATION` in exactly one translation unit, after
including the three dr headers.

| Function | Purpose |
| --- | --- |
| `drss_open_file(dec, path, hint)` | Opens a seekable file; `hint` may be `drss_format_unknown` for extension detection. |
| `drss_close(dec)` | Releases the underlying decoder. |
| `drss_read_pcm_frames_s16(dec, n, buf)` | Streams up to `n` interleaved s16 frames; `buf` may be `NULL` to skip. |
| `drss_seek_to_time(dec, seconds, &frame, &time)` | Seeks to the nearest frame and reports the actual landing point. |
| `drss_seek_to_pcm_frame(dec, frame)` | Seeks to an exact client frame index. |
| `drss_time_to_frame` / `drss_frame_to_time` | Pure time<->frame conversion (no I/O). |
| `drss_get_cursor_in_pcm_frames` / `drss_get_cursor_in_time` | Current decode position. |

The decoder struct exposes `format`, `channels`, `sampleRate` and
`totalPCMFrames` (0 if the length is unknown).

## Boundary handling

- **Rounding**: time -> frame uses round-to-nearest, half-down
  (`floor(t*rate + 0.5)`), so the time error is strictly less than one frame
  period (`1/sampleRate`), at most half a frame on average.
- **Negative / zero time**: resolves to frame 0, the first decodable frame.
- **Past end of stream**: when the length is known the target is clamped.
  WAV/FLAC land on the one-past-end frame (subsequent reads return 0 frames);
  MP3 cannot seek onto padding, so it lands on the last readable frame
  (`n-1`) and one final read returns that frame.
- **MP3 delay/padding**: dr_mp3's reported total includes encoder delay and
  padding. The wrapper subtracts both so `totalPCMFrames`, the cursor and all
  frame indices use client coordinates (the frames actually delivered),
  matching WAV/FLAC. The client-space cursor is maintained inside the wrapper
  because dr_mp3's internal cursor mixes delay-inclusive and client timelines.
- **Failed seek**: the underlying libraries restore their own position; treat
  the wrapper cursor as valid only when the seek returns true.
- **Unknown length** (`totalPCMFrames == 0`, e.g. untagged MP3 streams):
  seeking still works via dr_mp3's forward decode-and-discard, but end
  clamping is unavailable.

## Memory

No whole-file index or sample history is allocated. Resident state is the
fixed-size decoder union plus the caller's chunk buffer, both independent of
file length. Cost by format:

- WAV: direct byte seek, O(1).
- FLAC: seek table / binary search / in-frame skip, worst case O(log n) I/O
  with constant memory.
- MP3: frame-accurate decode-and-discard (no bound seek table); a backwards
  seek restarts the stream and advances to the target, trading time for
  bounded memory. This is what guarantees bit-identical output.

## Consistency verification

`test_stream_seek.c` builds a linear reference decode per file, then for 24
seek points per file (0, half/one frame, exact duration, past-end, and
deterministic pseudo-random interior points) seeks and compares every sample
through end of stream against the reference. It also records RSS before and
after all seeks.

Build and run:

    gcc -std=c99 -O2 -Wall -Wextra -Wpedantic test_stream_seek.c -lm -o test_stream_seek
    ./test_stream_seek vectors/*

Latest local run (gcc 15.2, generated via ffmpeg): 12 files across WAV/FLAC/
MP3, 44.1/48/22.05 kHz, mono/stereo, including a 960-frame file:

- 288 seek points, **0 sample mismatches** in every post-seek decode.
- All in-range time errors `< 1/sampleRate`; observed maxima ~1.1e-5 s at
  44.1 kHz and ~1.0e-5 s at 48 kHz (about half a frame).
- End/overflow seeks clamp as specified with 0 mismatches.
- RSS growth across 24 seeks per file: **0 KB** in every run.

`example.c` is a minimal streaming + seek demo:

    gcc -std=c99 -O2 example.c -lm -o example
    ./example vectors/tone48.mp3 3.5
