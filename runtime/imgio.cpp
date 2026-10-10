/* Leitura/gravacao de PNG pelo WIC (Windows Imaging Component, nativo do Windows): texturas substitutas e despejo de texturas. */
#include "imgio.h"
#include "util.h"
#include <windows.h>
#include <wincodec.h>
#include <string>
#include <stdio.h>

static IWICImagingFactory *factory() {
    static thread_local IWICImagingFactory *f; static thread_local bool tried;   /* uma fabrica por thread (leitura em segundo plano + despejo) */
    if (!tried) {
        tried = true;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)hr;   /* S_FALSE/RPC_E_CHANGED_MODE: a thread ja tem COM, serve */
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) f = nullptr;
    }
    return f;
}
bool img_load(const char *path, int *w, int *h, std::vector<uint32_t> &rgba) {
    IWICImagingFactory *f = factory(); if (!f) return false;
    std::wstring wp = to_wide(path);
    IWICBitmapDecoder *dec = nullptr; IWICBitmapFrameDecode *fr = nullptr; IWICFormatConverter *cv = nullptr; bool ok = false;
    if (SUCCEEDED(f->CreateDecoderFromFilename(wp.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &fr))) {
        UINT ww = 0, hh = 0; fr->GetSize(&ww, &hh);
        if (ww && hh && ww <= 16384 && hh <= 16384 && SUCCEEDED(f->CreateFormatConverter(&cv)) &&
            SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            rgba.resize((size_t)ww * hh);
            if (SUCCEEDED(cv->CopyPixels(nullptr, ww * 4, (UINT)(rgba.size() * 4), (BYTE *)rgba.data()))) { *w = (int)ww; *h = (int)hh; ok = true; }
        }
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release();
    return ok;
}
bool img_save_png(const char *path, int w, int h, const uint32_t *rgba) {
    IWICImagingFactory *f = factory(); if (!f) return false;
    std::wstring wp = to_wide(path);
    IWICStream *st = nullptr; IWICBitmapEncoder *enc = nullptr; IWICBitmapFrameEncode *fr = nullptr; IPropertyBag2 *pb = nullptr; bool ok = false;
    if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(wp.c_str(), GENERIC_WRITE)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, &pb)) && SUCCEEDED(fr->Initialize(pb)) &&
        SUCCEEDED(fr->SetSize((UINT)w, (UINT)h))) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;   /* o codificador PNG aceita BGRA */
        std::vector<uint32_t> bgra((size_t)w * h);
        for (size_t i = 0; i < bgra.size(); i++) { uint32_t p = rgba[i]; bgra[i] = (p & 0xff00ff00u) | ((p & 0xff) << 16) | ((p >> 16) & 0xff); }
        if (SUCCEEDED(fr->SetPixelFormat(&pf)) && pf == GUID_WICPixelFormat32bppBGRA && SUCCEEDED(fr->WritePixels((UINT)h, (UINT)w * 4, (UINT)((size_t)w * h * 4), (BYTE *)bgra.data())) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) ok = true;
    }
    if (pb) pb->Release(); if (fr) fr->Release(); if (enc) enc->Release(); if (st) st->Release();
    return ok;
}

bool img_save_jpg(const char *path, int w, int h, const uint32_t *rgba) {
    IWICImagingFactory *f = factory(); if (!f) return false;
    std::wstring wp = to_wide(path);
    IWICStream *st = nullptr; IWICBitmapEncoder *enc = nullptr; IWICBitmapFrameEncode *fr = nullptr; IPropertyBag2 *pb = nullptr; bool ok = false;
    if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(wp.c_str(), GENERIC_WRITE)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, &pb))) {
        PROPBAG2 opt = {}; opt.pstrName = (LPOLESTR)L"ImageQuality"; VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = 0.9f; pb->Write(1, &opt, &v);
        if (SUCCEEDED(fr->Initialize(pb)) && SUCCEEDED(fr->SetSize((UINT)w, (UINT)h))) {
            WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
            std::vector<uint8_t> bgr((size_t)w * h * 3);
            for (size_t i = 0; i < (size_t)w * h; i++) { const uint32_t p = rgba[i]; bgr[i * 3] = (uint8_t)(p >> 16); bgr[i * 3 + 1] = (uint8_t)(p >> 8); bgr[i * 3 + 2] = (uint8_t)p; }
            if (SUCCEEDED(fr->SetPixelFormat(&pf)) && pf == GUID_WICPixelFormat24bppBGR && SUCCEEDED(fr->WritePixels((UINT)h, (UINT)w * 3, (UINT)bgr.size(), bgr.data())) &&
                SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) ok = true;
        }
    }
    if (pb) pb->Release(); if (fr) fr->Release(); if (enc) enc->Release(); if (st) st->Release();
    return ok;
}
