// GIF compositing validator: loads a GIF via the DLL's WIC loader
// (stb_image_impl.cpp) and dumps every composited frame as raw BGRA so a
// Python/PIL script can cross-check the disposal/offset compositing.
#include <windows.h>
#include <wincodec.h>
#include <cstdio>
#include <cwchar>
#include <vector>

#include "../injecting Version/WatermarkDLL/stb_image_impl.cpp"

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        fwprintf(stderr, L"usage: giftest <in.gif> <out.raw>\n");
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    int *delays = nullptr, w = 0, h = 0, frames = 0, comp = 0;
    char narrowPath[1024];
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, narrowPath, sizeof(narrowPath), nullptr, nullptr);
    stbi_gif* gif = stbi_load_gif(narrowPath, &delays, &w, &h, &frames, &comp, 4);
    if (!gif) {
        fwprintf(stderr, L"load failed\n");
        return 2;
    }
    fwprintf(stdout, L"%d %d %d\n", w, h, frames);

    FILE* out = nullptr;
    if (_wfopen_s(&out, argv[2], L"wb") != 0 || !out) {
        fwprintf(stderr, L"out open failed\n");
        return 3;
    }
    // header: w,h,frames then per-frame delay + raw BGRA
    fwrite(&w, 4, 1, out);
    fwrite(&h, 4, 1, out);
    fwrite(&frames, 4, 1, out);
    for (int i = 0; i < frames; i++) {
        int d = gif->frames[i].delay;
        fwrite(&d, 4, 1, out);
        if (gif->frames[i].data) {
            fwrite(gif->frames[i].data, 1, (size_t)w * h * 4, out);
        } else {
            std::vector<unsigned char> zeros((size_t)w * h * 4, 0);
            fwrite(zeros.data(), 1, zeros.size(), out);
        }
    }
    fclose(out);
    fwprintf(stdout, L"ok\n");
    return 0;
}
