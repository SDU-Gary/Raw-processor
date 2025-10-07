#pragma once
#include <cstddef>
#include <vector>

namespace rawproc {

struct TileCoord {
    int x = 0; // tile index (not pixels)
    int y = 0; // tile index (not pixels)
    int lod = 0; // level of detail (0 = full res)
};

struct TileSpec {
    // Pixel-space rectangle of the inner tile region (without apron)
    int px = 0;
    int py = 0;
    int width = 0;
    int height = 0;
    // Apron in pixels around the tile (requested per stage)
    int apron = 0;
};

struct RenderRequest {
    int tileSize = 256; // square tile size in pixels for inner region
    int lod = 0;
    // Full output size in pixels (at the selected LOD)
    int outWidth = 0;
    int outHeight = 0;
    // If empty, the pipeline may compute a full-frame tile list. Otherwise, process only these tiles.
    std::vector<TileCoord> tiles;
};

} // namespace rawproc

