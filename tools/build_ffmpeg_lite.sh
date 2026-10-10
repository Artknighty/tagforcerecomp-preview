#!/bin/sh
# Builds the slim, LGPL-only FFmpeg that the release package ships: just libavcodec + libavutil with the three decoders the game needs
# (ATRAC3, ATRAC3+, H.264). No GPL components (no x264/x265/Xvid), no demuxers, filters or encoders, so the two DLLs are ~1.7 MB instead of
# the ~80 DLLs of a full FFmpeg build. The runtime loads them dynamically (runtime/ffmpeg_dec.cpp).
# Source: the official FFmpeg release below (it must match the FFmpeg headers the runtime is compiled against: MSYS2 package
# mingw-w64-ucrt-x86_64-ffmpeg, same version). Output: gen/ffmpeg_lite/{avcodec-NN.dll,avutil-NN.dll,libwinpthread-1.dll,FFMPEG-LGPL.txt}.
# Needs (build machine only): MSYS2 UCRT64 with make, nasm, gcc.  Run it from an MSYS2 shell:
#     MSYSTEM=UCRT64 C:/msys64/usr/bin/bash.exe -lc "sh tools/build_ffmpeg_lite.sh"
set -e
VER=${FFMPEG_VERSION:-9.0.2}
cd "$(dirname "$0")/.."
ROOT=$(pwd)
# build in a neutral absolute directory: FFmpeg embeds source paths and the configure line in the DLLs, so a path under a home folder
# would leak the builder's user name into a binary that gets published
W=${FFMPEG_BUILD_DIR:-/c/ffmpeg_lite_build}
mkdir -p $W gen/ffmpeg_lite
[ -f $W/ffmpeg-$VER.tar.xz ] || curl -sSL -o $W/ffmpeg-$VER.tar.xz https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz
[ -d $W/ffmpeg-$VER ] || tar -C $W -xf $W/ffmpeg-$VER.tar.xz
mkdir -p $W/bld && cd $W/bld
[ -f config.h ] || ../ffmpeg-$VER/configure --arch=x86_64 --target-os=mingw32 --enable-shared --disable-static --disable-autodetect \
    --disable-everything --disable-programs --disable-doc --disable-avformat --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-swresample --disable-network --disable-debug --enable-small --enable-decoder=atrac3,atrac3p,h264 --prefix=$W/out
make -j8
make install
cd $ROOT
cp $W/out/bin/avcodec-*.dll $W/out/bin/avutil-*.dll gen/ffmpeg_lite/
cp /ucrt64/bin/libwinpthread-1.dll gen/ffmpeg_lite/
cp $W/ffmpeg-$VER/COPYING.LGPLv2.1 gen/ffmpeg_lite/FFMPEG-LGPL.txt
ls -la gen/ffmpeg_lite
