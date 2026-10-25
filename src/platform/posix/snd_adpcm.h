#pragma once

// MS-ADPCM decoding for the SDL2 sound backend (and the Switch's later). Every loaded and streamed sound of the
// zombies zones is PCM16 or MS-ADPCM (format tag 2, the standard coefficients, block align 262 * channels, 512 frames
// a block); there is no WMA, so no ffmpeg. PCM16 needs no decode; this is the ADPCM path. Pure, so tests/platform can
// check it against ffmpeg's adpcm_ms.

#include <cstddef>
#include <cstdint>

// The samples a block holds, for sizing the output (the engine's blocks are 512; computed from the block align so the
// decoder is not tied to one encoder).
int Bo1_MsAdpcmFramesPerBlock(int channels, int blockAlign);

// Decodes `dataSize` bytes of MS-ADPCM (`channels` interleaved, `blockAlign` bytes a block) into interleaved int16
// PCM. Writes at most `outFrames` frames and returns the frames written; a short final block decodes what it holds.
int Bo1_DecodeMsAdpcm(const uint8_t *data, size_t dataSize, int channels, int blockAlign, int16_t *out, int outFrames);
