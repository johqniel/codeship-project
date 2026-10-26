#include "cuda_renderer.h"

#include <cuda_runtime.h>

#include <cstdio>

bool init_cuda_renderer() {
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA init error at cudaGetDeviceCount: %s\n", cudaGetErrorString(err));
        return false;
    }
    if (count <= 0) {
        std::fprintf(stderr, "CUDA init error: no CUDA devices found.\n");
        return false;
    }

    err = cudaSetDevice(0);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA init error at cudaSetDevice(0): %s\n", cudaGetErrorString(err));
        return false;
    }

    err = cudaFree(nullptr);
    if (err != cudaSuccess) {
        std::fprintf(stderr, "CUDA init error at cudaFree(nullptr): %s\n", cudaGetErrorString(err));
        return false;
    }

    return true;
}

void shutdown_cuda_renderer() {
    swaptube_shutdown_outer_billiards();
}

bool render_frame_cuda(
    uint32_t* host_pixels,
    int width,
    int height,
    const std::vector<Vec2>& vertices,
    const SwaptubeOuterBilliardsRenderConfig& cfg
) {
    return swaptube_render_outer_billiards(host_pixels, width, height, vertices, cfg);
}
