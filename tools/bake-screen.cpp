// Convert the indexed artwork to a read-only palette and pixels. Only the build machine needs libpng.
#include <png.h>
#include <cstdint>
#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    png_image image {};
    image.version = PNG_IMAGE_VERSION;
    if (argc != 2 || !png_image_begin_read_from_file (&image, argv[1])) return 1;
    if (image.width != 364 || image.height != 872 || !(image.format & PNG_FORMAT_FLAG_COLORMAP)) return 1;
    image.format = PNG_FORMAT_BGRA_COLORMAP;
    std::vector<uint8_t> pixels (PNG_IMAGE_SIZE (image));
    uint8_t palette[256][4] {};
    if (!png_image_finish_read (&image, nullptr, pixels.data(), 0, palette)) return 1;
    std::puts ("#pragma once\nnamespace foundry::art {\ninline constexpr uint32_t palette[] = {");
    for (auto& c : palette) std::printf ("0xff%02x%02x%02x,\n", c[2], c[1], c[0]);
    std::puts ("};\ninline constexpr unsigned char pixels[] = {");
    for (size_t i = 0; i < pixels.size(); ++i)
        std::printf ("%u,%c", pixels[i], (i % 32 == 31) ? '\n' : ' ');
    std::puts ("};\n}");
    png_image_free (&image);
    return std::ferror (stdout) ? 1 : 0;
}
