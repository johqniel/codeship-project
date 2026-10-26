#include "swaptube_outer_billiards_engine.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../swaptube/Host_Device_Shared/OuterBilliardsShared.h"

extern "C" void outer_billiards_table_render(
    uint32_t* d_pixels, const Cuda::ivec2& wh,
    const Cuda::vec2* h_verts, int n,
    float curvature, const Cuda::vec2& lx_ty, const Cuda::vec2& rx_by,
    uint32_t table_color, float table_opacity, uint32_t exterior_color);

extern "C" void outer_billiards_singularity_render(
    uint32_t* d_pixels, const Cuda::ivec2& wh,
    const Cuda::SingularityGraphParams& params);

namespace {

uint32_t* g_device_pixels = nullptr;
size_t g_device_capacity_pixels = 0;

bool ensure_device_buffer(size_t pixel_count) {
    if (g_device_pixels != nullptr && g_device_capacity_pixels >= pixel_count) return true;

    if (g_device_pixels != nullptr) {
        cudaFree(g_device_pixels);
        g_device_pixels = nullptr;
        g_device_capacity_pixels = 0;
    }

    if (cudaMalloc(&g_device_pixels, pixel_count * sizeof(uint32_t)) != cudaSuccess) {
        return false;
    }
    g_device_capacity_pixels = pixel_count;
    return true;
}

} // namespace

bool swaptube_render_outer_billiards(
    uint32_t* host_pixels,
    int width,
    int height,
    const std::vector<Vec2>& vertices,
    const SwaptubeOuterBilliardsRenderConfig& cfg
) {
    if (host_pixels == nullptr || width <= 0 || height <= 0) return false;
    if (vertices.size() < 3 || vertices.size() > static_cast<size_t>(Cuda::MAX_BILLIARD_VERTICES)) return false;

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (!ensure_device_buffer(pixel_count)) return false;

    if (cudaMemset(g_device_pixels, 0, pixel_count * sizeof(uint32_t)) != cudaSuccess) {
        return false;
    }

    const float aspect = static_cast<float>(width) / std::max(1, height);
    const float hw = cfg.view_half_width;
    const float hh = hw / aspect;
    const Cuda::vec2 lx_ty(cfg.center_x - hw, cfg.center_y - hh);
    const Cuda::vec2 rx_by(cfg.center_x + hw, cfg.center_y + hh);
    const Cuda::ivec2 wh(width, height);

    std::vector<Cuda::vec2> host_verts;
    host_verts.reserve(vertices.size());
    for (const Vec2& v : vertices) {
        host_verts.emplace_back(static_cast<float>(v.x), static_cast<float>(v.y));
    }

    outer_billiards_table_render(
        g_device_pixels,
        wh,
        host_verts.data(),
        static_cast<int>(host_verts.size()),
        cfg.curvature,
        lx_ty,
        rx_by,
        cfg.table_color,
        cfg.table_opacity,
        cfg.exterior_color
    );

    Cuda::SingularityGraphParams params{};
    const int n = static_cast<int>(host_verts.size());
    params.n = n;
    for (int i = 0; i < n; i++) params.verts[i] = host_verts[static_cast<size_t>(i)];

    params.curvature = cfg.curvature;
    params.lx_ty = lx_ty;
    params.rx_by = rx_by;
    params.world_per_pixel = (rx_by.y - lx_ty.y) / std::max(1, height);
    params.line_width_scale = cfg.line_width_scale * (static_cast<float>(height) / 1080.0f);

    params.web_opacity = std::clamp(cfg.singularity_opacity, 0.0f, 1.0f);
    params.depth = std::max(0.0f, cfg.singularity_depth);
    params.line_color = cfg.line_color;
    params.singularity_rainbow = std::clamp(cfg.singularity_rainbow, 0.0f, 1.0f);

    params.island_opacity = std::clamp(cfg.island_opacity, 0.0f, 1.0f);
    params.max_period = std::max(1, cfg.max_period);
    params.island_depth = std::max(1, static_cast<int>(std::ceil(params.depth * cfg.island_boundary_depth)));
    params.periodicity_or_flow = std::clamp(cfg.periodicity_or_flow, 0.0f, 1.0f);
    params.flow_depth = std::max(0.0f, cfg.flow_depth);
    params.cycle_highlight = cfg.cycle_highlight;
    params.cycle_highlight_enable = std::clamp(cfg.cycle_highlight_enable, 0.0f, 1.0f);

    outer_billiards_singularity_render(g_device_pixels, wh, params);

    if (cudaMemcpy(host_pixels, g_device_pixels, pixel_count * sizeof(uint32_t), cudaMemcpyDeviceToHost) != cudaSuccess) {
        return false;
    }
    return true;
}

void swaptube_shutdown_outer_billiards() {
    if (g_device_pixels != nullptr) {
        cudaFree(g_device_pixels);
        g_device_pixels = nullptr;
        g_device_capacity_pixels = 0;
    }
}
