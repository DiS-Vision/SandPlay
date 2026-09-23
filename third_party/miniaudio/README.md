# Vendoring miniaudio

This directory expects `miniaudio.h` (the single-header library) to sit
right here, next to `miniaudio_impl.c`. It isn't included in this scaffold
because it needs to be downloaded from its actual source rather than
reproduced from memory.

1. Download the latest single header from https://github.com/mackron/miniaudio
   (`miniaudio.h`, currently ~1 file, no dependencies)
2. Place it at `third_party/miniaudio/miniaudio.h`
3. Build as usual — `miniaudio_impl.c` already has `#define MINIAUDIO_IMPLEMENTATION`
   before including it, so nothing else needs to change.

miniaudio decodes WAV, FLAC and MP3 natively (via its bundled dr_wav,
dr_flac and minimp3 — no ffmpeg, no system codec dependency), and exposes
a synchronous frame-cursor API (`ma_sound_get_cursor_in_pcm_frames` /
`ma_sound_seek_to_pcm_frame`) that `AudioEngine` uses for the real
position-based progress bar and seeking.
