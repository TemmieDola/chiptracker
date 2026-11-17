# vendor/

## libopenmpt

* Version: **0.8.10** (autotools source tarball, `libopenmpt-0.8.10+release.autotools.tar.gz`)
* Source: https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.10+release.autotools.tar.gz
* Built for: `arm-funkey-linux-musleabihf` (FunKey SDK 2.3.0, GCC 10.2, ARMv7 + NEON)
* Output: `lib/libopenmpt.a`, public C headers in `include/libopenmpt/`
* Configuration: static, zlib + mpg123 (MO3 MP3 samples), **no** OGG/Vorbis,
  no openmpt123/examples/tests/doxygen.
* Build: `bash scripts/build-libopenmpt.sh` (cached; only rebuilds if the
  archive is missing).
* License: BSD-3-Clause, see `../chiptune/opk/LICENSE.txt` and the libopenmpt
  source tree.

The archive is big in source form; the final `.opk` only contains the linked
and stripped `chiptune` binary.
