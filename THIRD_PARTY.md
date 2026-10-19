# Third-party software and content

This project's own code and documentation are under the [MIT license](LICENSE). This repository contains **no game data**. The project builds on, links to, or interoperates with the following; each keeps its own license.

| Component | Use | License |
|---|---|---|
| [FFmpeg](https://ffmpeg.org/) 9.0.2 (`libavcodec`, `libavutil`) | decoding of H.264 movies and ATRAC3/3+ audio; loaded **dynamically** at run time. The release zip carries a **slim, LGPL-only build** made by `tools/build/build_ffmpeg_lite.sh` from the official source tarball (`./configure --disable-everything --enable-decoder=atrac3,atrac3p,h264 --disable-avformat ...`, no GPL components). You can replace the two DLLs with your own build of the same FFmpeg version. Corresponding source: https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz . The repository itself contains no FFmpeg code | LGPL-2.1+ |
| [winpthreads](https://www.mingw-w64.org/) (`libwinpthread-1.dll`) | runtime library of the FFmpeg DLLs, from MSYS2 | MIT / ZPL |
| [TinyCC](https://bellard.org/tcc/) (`libtcc.dll`, headers, `libtcc1-64.a`; release 0.9.27) | compiles the translated game code in memory at start-up; shipped unmodified in the release zip, downloaded from the official release by `tools/package_release.py` (source: https://repo.or.cz/tinycc.git) | LGPL-2.1 |
| [Python](https://www.python.org/) embeddable package (3.11) | runs the Allegrex -> C translator on the player's machine; shipped unmodified in the release zip | PSF License |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) (`llama-server`) | runs the small language model of the Better AI mod; distributed with the **mod package**, not in this repository | MIT |
| Qwen2.5-0.5B-Instruct (Alibaba Cloud) | base model that the Better AI mod fine-tunes with LoRA; the fine-tuned weights are distributed with the mod package, not in this repository | Apache-2.0 |
| [PPSSPP](https://www.ppsspp.org/) | **read-only reference** for hardware behavior and for the texture-pack format; **no code is copied** | GPL-2.0+ |
| Microsoft Windows APIs, Direct3D 11, [Vulkan](https://www.vulkan.org/) | rendering and platform | platform / Khronos terms |
| XXH64 (Yann Collet's public algorithm) | hash used by PPSSPP-format texture packs; **own implementation** in `runtime/core/xxh.h`, no xxHash code copied | — |
| PPSSPP-format HD texture packs (MikeyMK, Filipe Ramos) | **not distributed here**; credited in the README | their authors' terms |

## Trademarks and game content

Yu-Gi-Oh! and Tag Force are trademarks of Konami and its licensors. This project is **not affiliated with or endorsed by Konami**.
You must own a legitimate copy of the game. Card names and numbers appear in documentation and tools only as references to your
own copy's data; the repository does not ship the card database, deck lists, images, audio or video.
