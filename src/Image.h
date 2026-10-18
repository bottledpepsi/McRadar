// Image.h - a decoded raster image (premultiplied BGRA, the format Direct2D wants).
#pragma once
#include <cstdint>
#include <vector>

struct Image
{
    int w = 0, h = 0;
    std::vector<uint8_t> bgra; // w*h*4, premultiplied alpha
    uint64_t id = 0; // unique per image: the renderer caches its GPU bitmap under this id
};
