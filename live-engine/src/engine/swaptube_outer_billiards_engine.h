#pragma once

#include <cstdint>
#include <vector>

#include "../vec2.h"

struct SwaptubeOuterBilliardsRenderConfig {
    float curvature = 0.0f;
    float view_half_width = 7.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;

    float table_opacity = 1.0f;
    uint32_t table_color = 0xff1a8b3a;
    uint32_t exterior_color = 0xff000000;

    float singularity_opacity = 0.0f;
    float singularity_depth = 0.0f;
    float singularity_rainbow = 0.0f;

    float island_opacity = 0.0f;
    float periodicity_or_flow = 0.0f;
    float flow_depth = 0.0f;

    float cycle_highlight = 0.0f;
    float cycle_highlight_enable = 0.0f;

    int max_period = 100;
    float island_boundary_depth = 3.0f;
    float line_width_scale = 1.0f;
    uint32_t line_color = 0xffffffff;
};

bool swaptube_render_outer_billiards(
    uint32_t* host_pixels,
    int width,
    int height,
    const std::vector<Vec2>& vertices,
    const SwaptubeOuterBilliardsRenderConfig& cfg
);

void swaptube_shutdown_outer_billiards();
