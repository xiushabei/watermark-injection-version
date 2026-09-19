// stb_image_impl.cpp - Image loading implementation using WIC (Windows Imaging Component)
// Supports PNG, JPG, JPEG, BMP, GIF, TIFF and other formats

#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// Type definitions
typedef unsigned char stbi_uc;

// GIF frame structure
struct stbi_gif_frame {
    int delay;
    stbi_uc *data;
};

// GIF structure
struct stbi_gif {
    int w, h, count;
    stbi_gif_frame *frames;
};

// Global WIC factory (initialized once)
static IWICImagingFactory* g_wicFactory = nullptr;
static std::mutex g_wicMutex;

// Initialize WIC factory
static bool initWIC() {
    std::lock_guard<std::mutex> lock(g_wicMutex);
    if (g_wicFactory) return true;
    
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wicFactory)
    );
    
    if (FAILED(hr)) {
        OutputDebugStringA("[WatermarkDLL] Failed to create WIC factory\n");
        return false;
    }
    
    OutputDebugStringA("[WatermarkDLL] WIC factory initialized\n");
    return true;
}

// Convert WIC pixel format to GUID for 32bppBGRA
static GUID getConvertFormat() {
    return GUID_WICPixelFormat32bppBGRA;
}

// Load image using WIC
static stbi_uc* loadWithWIC(const wchar_t* filename, int* width, int* height) {
    if (!initWIC()) return nullptr;
    
    // Create decoder from file
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    
    if (FAILED(hr)) {
        OutputDebugStringA("[WatermarkDLL] Failed to create WIC decoder\n");
        return nullptr;
    }
    
    // Get first frame
    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        decoder->Release();
        OutputDebugStringA("[WatermarkDLL] Failed to get WIC frame\n");
        return nullptr;
    }
    
    // Get frame size
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    
    if (w == 0 || h == 0) {
        frame->Release();
        decoder->Release();
        return nullptr;
    }
    
    // Convert to 32bppBGRA format
    IWICFormatConverter* converter = nullptr;
    hr = g_wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        frame->Release();
        decoder->Release();
        return nullptr;
    }
    
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return nullptr;
    }
    
    // Copy pixels
    stbi_uc* pixels = (stbi_uc*)malloc(w * h * 4);
    if (!pixels) {
        converter->Release();
        frame->Release();
        decoder->Release();
        return nullptr;
    }
    
    hr = converter->CopyPixels(
        nullptr,
        w * 4,  // stride
        w * h * 4,  // buffer size
        pixels
    );
    
    if (FAILED(hr)) {
        free(pixels);
        converter->Release();
        frame->Release();
        decoder->Release();
        return nullptr;
    }
    
    // Set output dimensions
    if (width) *width = (int)w;
    if (height) *height = (int)h;
    
    // Cleanup
    converter->Release();
    frame->Release();
    decoder->Release();
    
    return pixels;
}

// Load GIF with multiple frames
static stbi_gif* loadGifWithWIC(const wchar_t* filename, int** delays, int* x, int* y, int* z, int* comp) {
    if (!initWIC()) return nullptr;
    
    // Create decoder from file
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    
    if (FAILED(hr)) {
        OutputDebugStringA("[WatermarkDLL] Failed to create GIF decoder\n");
        return nullptr;
    }
    
    // Get frame count
    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0) {
        decoder->Release();
        return nullptr;
    }
    
    // Get first frame to get dimensions
    IWICBitmapFrameDecode* firstFrame = nullptr;
    hr = decoder->GetFrame(0, &firstFrame);
    if (FAILED(hr)) {
        decoder->Release();
        return nullptr;
    }
    
    UINT w = 0, h = 0;
    firstFrame->GetSize(&w, &h);
    firstFrame->Release();
    
    if (w == 0 || h == 0) {
        decoder->Release();
        return nullptr;
    }
    
    // Create GIF structure
    stbi_gif* gif = (stbi_gif*)malloc(sizeof(stbi_gif));
    if (!gif) {
        decoder->Release();
        return nullptr;
    }
    
    gif->w = (int)w;
    gif->h = (int)h;
    gif->count = (int)frameCount;
    gif->frames = (stbi_gif_frame*)malloc(sizeof(stbi_gif_frame) * frameCount);
    
    if (!gif->frames) {
        free(gif);
        decoder->Release();
        return nullptr;
    }
    
    // Allocate delays array if requested
    if (delays) {
        *delays = (int*)malloc(sizeof(int) * frameCount);
    }

    // Full-size compositing canvas (zero-init = transparent background; the old
    // code uploaded per-frame buffers straight from malloc, which showed garbage
    // wherever a frame didn't cover the full image - the "background garbling").
    stbi_uc* canvas = (stbi_uc*)calloc((size_t)w * h * 4, 1);
    // Snapshot of the canvas before the previous frame, for disposal=3
    stbi_uc* snapshot = (stbi_uc*)malloc((size_t)w * h * 4);
    bool haveSnapshot = false;
    int prevDisposal = 1, prevLeft = 0, prevTop = 0, prevW = 0, prevH = 0;

    if (!canvas || !snapshot) {
        free(canvas); free(snapshot);
        free(gif->frames); free(gif);
        decoder->Release();
        return nullptr;
    }

    // Load and composite each frame. GIF frames may be partial updates placed at
    // an offset (/imgdesc/Left|Top) and declare a disposal method that says what
    // happens to them before the next frame is drawn - ignoring either caused
    // the playback displacement and leftover artifacts.
    for (UINT i = 0; i < frameCount; i++) {
        IWICBitmapFrameDecode* frame = nullptr;
        hr = decoder->GetFrame(i, &frame);
        if (FAILED(hr)) {
            gif->frames[i].data = nullptr;
            gif->frames[i].delay = 100;
            continue;
        }

        int delay = 100;
        int disposal = 1;
        int left = 0, top = 0;
        UINT fw = 0, fh = 0;
        frame->GetSize(&fw, &fh);

        IWICMetadataQueryReader* meta = nullptr;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&meta))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Delay", &v)) && v.vt == VT_UI2) {
                delay = (int)v.uiVal * 10;  // GIF delay is in 1/100s -> ms
                if (delay < 20) delay = 100;
            }
            PropVariantClear(&v); PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Disposal", &v)) && v.vt == VT_UI1) {
                disposal = (int)v.bVal;
            }
            PropVariantClear(&v); PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Left", &v)) && v.vt == VT_UI2) {
                left = (int)v.uiVal;
            }
            PropVariantClear(&v); PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Top", &v)) && v.vt == VT_UI2) {
                top = (int)v.uiVal;
            }
            PropVariantClear(&v);
            meta->Release();
        }

        if (fw > 0 && fh > 0) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(g_wicFactory->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                stbi_uc* fpix = (stbi_uc*)malloc((size_t)fw * fh * 4);
                if (fpix && SUCCEEDED(conv->CopyPixels(nullptr, fw * 4, fw * fh * 4, fpix))) {
                    // 1) honor the PREVIOUS frame's disposal
                    if (prevDisposal == 2) {
                        // restore to background: clear the previous frame's rect
                        for (int r = 0; r < prevH; r++) {
                            int cy = prevTop + r;
                            if (cy < 0 || cy >= (int)h) continue;
                            memset(canvas + ((size_t)cy * w + prevLeft) * 4, 0, (size_t)prevW * 4);
                        }
                    } else if (prevDisposal == 3 && haveSnapshot) {
                        memcpy(canvas, snapshot, (size_t)w * h * 4);
                    }
                    // 2) if THIS frame restores-to-previous, snapshot first
                    if (disposal == 3) {
                        memcpy(snapshot, canvas, (size_t)w * h * 4);
                        haveSnapshot = true;
                    }
                    // 3) composite the current frame over the canvas (straight alpha)
                    for (UINT r = 0; r < fh; r++) {
                        int cy = top + (int)r;
                        if (cy < 0 || cy >= (int)h) continue;
                        for (UINT c = 0; c < fw; c++) {
                            int cx = left + (int)c;
                            if (cx < 0 || cx >= (int)w) continue;
                            const stbi_uc* src = fpix + ((size_t)r * fw + c) * 4;
                            stbi_uc* dst = canvas + ((size_t)cy * w + cx) * 4;
                            unsigned int a = src[3];
                            if (a == 255) {
                                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
                            } else if (a > 0) {
                                unsigned int ia = 255 - a;
                                dst[0] = (stbi_uc)((src[0] * a + dst[0] * ia) / 255);
                                dst[1] = (stbi_uc)((src[1] * a + dst[1] * ia) / 255);
                                dst[2] = (stbi_uc)((src[2] * a + dst[2] * ia) / 255);
                                dst[3] = 255;
                            }
                        }
                    }
                    prevLeft = left; prevTop = top;
                    prevW = (int)fw; prevH = (int)fh;
                }
                free(fpix);
                if (conv) conv->Release();
            }
        }
        frame->Release();

        // 4) output a full-size copy of the composited canvas as this frame
        stbi_uc* out = (stbi_uc*)malloc((size_t)w * h * 4);
        if (out) memcpy(out, canvas, (size_t)w * h * 4);
        gif->frames[i].data = out;
        gif->frames[i].delay = delay;
        if (delays) (*delays)[i] = delay;

        prevDisposal = disposal;
    }

    free(canvas);
    free(snapshot);

    // Set output values
    if (x) *x = (int)w;
    if (y) *y = (int)h;
    if (z) *z = (int)frameCount;
    if (comp) *comp = 4;

    decoder->Release();
    return gif;
}

// Exported functions

extern "C" stbi_uc* stbi_load(const char* filename, int* x, int* y, int* channels_in_file, int desired_channels) {
    if (!filename) return nullptr;
    
    // Convert to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, nullptr, 0);
    if (len <= 0) return nullptr;
    
    std::wstring wfilename(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, &wfilename[0], len);
    
    // Load image
    stbi_uc* pixels = loadWithWIC(wfilename.c_str(), x, y);
    
    if (pixels && channels_in_file) {
        *channels_in_file = 4; // Always BGRA
    }
    
    return pixels;
}

extern "C" void stbi_image_free(void* retval_from_stbi_load) {
    if (retval_from_stbi_load) {
        free(retval_from_stbi_load);
    }
}

extern "C" stbi_gif* stbi_load_gif(const char* filename, int** delays, int* x, int* y, int* z, int* comp, int req_comp) {
    if (!filename) return nullptr;
    
    // Convert to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, nullptr, 0);
    if (len <= 0) return nullptr;
    
    std::wstring wfilename(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, &wfilename[0], len);
    
    return loadGifWithWIC(wfilename.c_str(), delays, x, y, z, comp);
}

extern "C" void stbi_gif_free(stbi_gif* gif) {
    if (gif) {
        if (gif->frames) {
            for (int i = 0; i < gif->count; i++) {
                if (gif->frames[i].data) {
                    free(gif->frames[i].data);
                }
            }
            free(gif->frames);
        }
        free(gif);
    }
}
