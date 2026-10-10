# Third-party software and content

This project's own code and documentation are under the [MIT license](LICENSE). This repository contains **no game data**. The project builds on, links to, or interoperates with the following; each keeps its own license.

| Component | Use | License |
|---|---|---|
| [FFmpeg](https://ffmpeg.org/) (`libavcodec`, `libavutil`) | decoding of H.264 movies and ATRAC audio; loaded **dynamically** at run time from the DLLs of your own MSYS2 install (`tools/ffmpeg_dlls.py` copies them next to the executable); not part of this repository | LGPL/GPL depending on the build — check it if you redistribute binaries |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) (`llama-server`) | runs the small language model of the Better AI mod; distributed with the **mod package**, not in this repository | MIT |
| Qwen2.5-0.5B-Instruct (Alibaba Cloud) | base model that the Better AI mod fine-tunes with LoRA; the fine-tuned weights are distributed with the mod package, not in this repository | Apache-2.0 |
| [PPSSPP](https://www.ppsspp.org/) | **read-only reference** for hardware behavior and for the texture-pack format; **no code is copied** | GPL-2.0+ |
| Microsoft Windows APIs, Direct3D 11, [Vulkan](https://www.vulkan.org/) | rendering and platform | platform / Khronos terms |
| XXH64 (Yann Collet's public algorithm) | hash used by PPSSPP-format texture packs; **own implementation** in `runtime/xxh.h`, no xxHash code copied | — |
| PPSSPP-format HD texture packs (MikeyMK, Filipe Ramos) | **not distributed here**; credited in the README | their authors' terms |

## Trademarks and game content

Yu-Gi-Oh! and Tag Force are trademarks of Konami and its licensors. This project is **not affiliated with or endorsed by Konami**.
You must own a legitimate copy of the game. Card names and numbers appear in documentation and tools only as references to your
own copy's data; the repository does not ship the card database, deck lists, images, audio or video.
