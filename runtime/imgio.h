/* PNG (e outros formatos do WIC) <-> RGBA 8 bits (bytes R,G,B,A; uint32 little-endian 0xAABBGGRR). */
#pragma once
#include <stdint.h>
#include <vector>
bool img_load(const char *path_utf8, int *w, int *h, std::vector<uint32_t> &rgba);
bool img_save_png(const char *path_utf8, int w, int h, const uint32_t *rgba);
bool img_save_jpg(const char *path_utf8, int w, int h, const uint32_t *rgba);   /* JPEG qualidade 0,9 (capturas de diagnostico) */
