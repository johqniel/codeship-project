#pragma once

#include <cstdint>
#include <vector>

#include "vec2.h"
#include "engine/swaptube_outer_billiards_engine.h"

bool init_cuda_renderer();
void shutdown_cuda_renderer();

// Renders one frame on GPU and copies RGB pixels back to host buffer.
// host_pixels must contain width*height elements.
bool render_frame_cuda(
    uint32_t* host_pixels,
    int width,
    int height,
    const std::vector<Vec2>& vertices,
    const SwaptubeOuterBilliardsRenderConfig& cfg
);
