#!/bin/bash
# Makes the MS-ADPCM test vectors for adpcm_test and runs it on each. For every case: a sine is encoded to MS-ADPCM
# with ffmpeg, ffmpeg decodes it back to raw s16le (the reference), and adpcm_test checks Bo1_DecodeMsAdpcm matches
# that reference sample for sample. The engine's on-disk blocks are 262 bytes a channel (snd_driver_xaudio2.cpp:410,
# 512 frames a block); ffmpeg's adpcm_ms encoder only writes power-of-two block sizes, so the stride is covered with
# 512- and 1024-byte blocks instead, mono and stereo. The decoder reads the block align from the stream, so the stride
# is the only thing that changes with it, and the math is what these vectors pin down.
#
#   adpcm_gen_vectors.sh <adpcm_test binary> <work dir>
set -eu
BIN=${1:?usage: adpcm_gen_vectors.sh <adpcm_test> <workdir>}
D=${2:?usage: adpcm_gen_vectors.sh <adpcm_test> <workdir>}
mkdir -p "$D"
fails=0

run_case() {
    local name=$1 ch=$2; shift 2
    ffmpeg -nostdin -v error -y -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1.3" \
        -ac "$ch" -c:a pcm_s16le "$D/${name}_src.wav"
    ffmpeg -nostdin -v error -y -i "$D/${name}_src.wav" -c:a adpcm_ms "$@" "$D/${name}.wav"
    ffmpeg -nostdin -v error -y -i "$D/${name}.wav" -f s16le -c:a pcm_s16le "$D/${name}_ref.s16le"
    "$BIN" "$D/${name}.wav" "$D/${name}_ref.s16le" || fails=$((fails + 1))
}

run_case mono_default 1
run_case stereo_default 2
run_case mono_512 1 -block_size 512
run_case stereo_512 2 -block_size 512

# Fail-first: a single corrupted reference sample must be caught, so a PASS means something.
cp "$D/mono_default_ref.s16le" "$D/corrupt.s16le"
printf '\xde\x7f' | dd of="$D/corrupt.s16le" bs=1 seek=4000 count=2 conv=notrunc status=none
if "$BIN" "$D/mono_default.wav" "$D/corrupt.s16le" >/dev/null 2>&1; then
    echo "FAIL: a corrupted reference was not caught"
    fails=$((fails + 1))
else
    echo "PASS: a corrupted reference is caught (fail-first)"
fi

echo "adpcm vectors: $([ $fails -eq 0 ] && echo 'all passed' || echo "$fails FAILED")"
exit $((fails == 0 ? 0 : 1))
