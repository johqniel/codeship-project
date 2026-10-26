#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <random>
#include <unordered_map>
#include <vector>

#include "cuda_renderer.h"
#include "outer_billiards.h"
#include "vec2.h"

struct AppState {
    int width = 1920;
    int height = 1080;

    std::vector<Vec2> vertices = {
        Vec2(-1.0, -1.0),
        Vec2( 1.0, -1.0),
        Vec2( 1.0,  1.0),
        Vec2(-1.0,  1.0),
    };

    bool hyperbolic_enabled = false;
    double curvature = 0.0;
    int flow_depth = 20;
    double center_x = 0.0;
    double center_y = 0.0;

    double table_opacity = 1.0;
    double island_opacity = 0.0;
    double singularity_opacity = 0.0;
    double singularity_depth = 0.0;
    double singularity_rainbow = 0.0;
    double periodicity_or_flow = 0.0;
    double cycle_highlight = 0.0;
    double cycle_highlight_enable = 0.0;
    bool show_debug_handles = false;

    double view_half_width = 7.0;
    int sample_stride = 1;
    int resolution_mode = 1; // 0=fast, 1=high, 2=very-high

    bool dragging = false;
    int dragged_vertex = -1;
    bool paused = false;
    bool show_help = true;
    bool show_sliders = true;
    int mouse_x = 0;
    int mouse_y = 0;
    int active_slider = -1;

    bool idle_jitter_enabled = true;
    bool idle_jitter_active = false;
    double idle_jitter_start_delay_sec = 3.0;
    double idle_drift_speed = 0.00045;
    double idle_turn_jitter = 0.00014;

    bool hyperbolic_script_active = false;
    double hyperbolic_script_time_sec = 0.0;
    bool auto_play_sequence_if_idle = true;
    double auto_play_start_delay_sec = 1.0;
    bool auto_play_consumed = false;
};

enum class SliderId {
    ViewHalfWidth,
    CenterX,
    CenterY,
    FlowDepth,
    Curvature,
    TableOpacity,
    IslandOpacity,
    SingularityOpacity,
    SingularityDepth,
    SingularityRainbow,
    PeriodicityOrFlow,
    CycleHighlight,
    CycleHighlightEnable,
};

struct SliderSpec {
    SliderId id;
    const char* label;
    double min_value;
    double max_value;
    bool integer_mode;
    uint32_t color;
};

struct SliderRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct SliderLayoutEntry {
    SliderSpec spec;
    SliderRect rail;
};

struct DriftState {
    std::vector<double> heading;
    std::vector<double> omega;
    std::vector<double> speed;
};

struct ResolutionPreset {
    int width;
    int height;
    int stride;
    const char* label;
};

static const ResolutionPreset RES_PRESETS[] = {
    {1280,  720, 2, "fast"},
    {1920, 1080, 1, "high"},
    {3840, 2160, 1, "very-high"},
};

static const char* LIVE_CONFIG_PATH = "live_config.txt";

static constexpr double BILLIARDS_PHASE = M_PI * 1.25;
static constexpr double BILLIARDS_K_PENTAGON = -0.472135955;
static constexpr double BILLIARDS_K_TRIOCT = -0.01348004;
static constexpr double BILLIARDS_K_END = -0.05;
static constexpr double BILLIARDS_ZOOM_MIN = -2.5;
static constexpr double BILLIARDS_ZOOM_MAX = 0.3;

static void apply_resolution_preset(AppState& state, int mode);
static double zoom_to_view_half_width(const AppState& st, double zoom_value);

static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return ((r & 255u) << 16) | ((g & 255u) << 8) | (b & 255u);
}

static uint32_t blend_rgb(uint32_t dst, uint32_t src, double alpha) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    const double ia = 1.0 - alpha;
    const unsigned dr = (dst >> 16) & 255u;
    const unsigned dg = (dst >> 8) & 255u;
    const unsigned db = dst & 255u;
    const unsigned sr = (src >> 16) & 255u;
    const unsigned sg = (src >> 8) & 255u;
    const unsigned sb = src & 255u;
    const unsigned r = static_cast<unsigned>(dr * ia + sr * alpha);
    const unsigned g = static_cast<unsigned>(dg * ia + sg * alpha);
    const unsigned b = static_cast<unsigned>(db * ia + sb * alpha);
    return rgb(r, g, b);
}

static uint32_t hsv_to_rgb(double h, double s, double v) {
    h = std::fmod(std::fmod(h, 1.0) + 1.0, 1.0);
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    double c = v * s;
    double x = c * (1.0 - std::abs(std::fmod(h * 6.0, 2.0) - 1.0));
    double m = v - c;

    double r1 = 0.0, g1 = 0.0, b1 = 0.0;
    if (h < 1.0 / 6.0)      { r1 = c; g1 = x; b1 = 0.0; }
    else if (h < 2.0 / 6.0) { r1 = x; g1 = c; b1 = 0.0; }
    else if (h < 3.0 / 6.0) { r1 = 0.0; g1 = c; b1 = x; }
    else if (h < 4.0 / 6.0) { r1 = 0.0; g1 = x; b1 = c; }
    else if (h < 5.0 / 6.0) { r1 = x; g1 = 0.0; b1 = c; }
    else                    { r1 = c; g1 = 0.0; b1 = x; }

    return rgb(
        static_cast<unsigned>((r1 + m) * 255.0),
        static_cast<unsigned>((g1 + m) * 255.0),
        static_cast<unsigned>((b1 + m) * 255.0)
    );
}

static Vec2 pixel_to_world(const AppState& st, int px, int py) {
    const double aspect = static_cast<double>(st.width) / std::max(1, st.height);
    const double hw = st.view_half_width;
    const double hh = hw / aspect;

    const double x = st.center_x - hw + (2.0 * hw) * (static_cast<double>(px) / std::max(1, st.width - 1));
    const double y = st.center_y + hh - (2.0 * hh) * (static_cast<double>(py) / std::max(1, st.height - 1));
    return Vec2(x, y);
}

static Vec2 world_to_pixel(const AppState& st, const Vec2& p) {
    const double aspect = static_cast<double>(st.width) / std::max(1, st.height);
    const double hw = st.view_half_width;
    const double hh = hw / aspect;

    const double nx = (p.x - (st.center_x - hw)) / (2.0 * hw);
    const double ny = ((st.center_y + hh) - p.y) / (2.0 * hh);

    return Vec2(nx * (st.width - 1), ny * (st.height - 1));
}

static void put_pixel(std::vector<uint32_t>& img, int w, int h, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    img[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = c;
}

static void draw_line(std::vector<uint32_t>& img, int w, int h, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        put_pixel(img, w, h, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_circle_filled(std::vector<uint32_t>& img, int w, int h, int cx, int cy, int radius, uint32_t c) {
    const int r2 = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= r2) {
                put_pixel(img, w, h, cx + x, cy + y, c);
            }
        }
    }
}

static void fill_rect_alpha(std::vector<uint32_t>& img, int w, int h, int x, int y, int rw, int rh, uint32_t c, double alpha) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(w, x + rw);
    const int y1 = std::min(h, y + rh);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            uint32_t& dst = img[static_cast<size_t>(yy) * static_cast<size_t>(w) + static_cast<size_t>(xx)];
            dst = blend_rgb(dst, c, alpha);
        }
    }
}

static double get_slider_value(const AppState& st, SliderId id) {
    switch (id) {
        case SliderId::ViewHalfWidth: return st.view_half_width;
        case SliderId::CenterX: return st.center_x;
        case SliderId::CenterY: return st.center_y;
        case SliderId::FlowDepth: return static_cast<double>(st.flow_depth);
        case SliderId::Curvature: return st.curvature;
        case SliderId::TableOpacity: return st.table_opacity;
        case SliderId::IslandOpacity: return st.island_opacity;
        case SliderId::SingularityOpacity: return st.singularity_opacity;
        case SliderId::SingularityDepth: return st.singularity_depth;
        case SliderId::SingularityRainbow: return st.singularity_rainbow;
        case SliderId::PeriodicityOrFlow: return st.periodicity_or_flow;
        case SliderId::CycleHighlight: return st.cycle_highlight;
        case SliderId::CycleHighlightEnable: return st.cycle_highlight_enable;
    }
    return 0.0;
}

static void set_slider_value(AppState& st, SliderId id, double v) {
    switch (id) {
        case SliderId::ViewHalfWidth:
            st.view_half_width = std::clamp(v, 0.1, 200.0);
            break;
        case SliderId::CenterX:
            st.center_x = std::clamp(v, -1e6, 1e6);
            break;
        case SliderId::CenterY:
            st.center_y = std::clamp(v, -1e6, 1e6);
            break;
        case SliderId::FlowDepth:
            st.flow_depth = std::clamp(static_cast<int>(std::round(v)), 1, 10000);
            break;
        case SliderId::Curvature:
            st.curvature = std::clamp(v, -2.0, -1e-7);
            st.hyperbolic_enabled = true;
            break;
        case SliderId::TableOpacity:
            st.table_opacity = std::clamp(v, 0.0, 1.0);
            break;
        case SliderId::IslandOpacity:
            st.island_opacity = std::clamp(v, 0.0, 1.0);
            break;
        case SliderId::SingularityOpacity:
            st.singularity_opacity = std::clamp(v, 0.0, 1.0);
            break;
        case SliderId::SingularityDepth:
            st.singularity_depth = std::clamp(v, 0.0, 1e6);
            break;
        case SliderId::SingularityRainbow:
            st.singularity_rainbow = std::clamp(v, 0.0, 1.0);
            break;
        case SliderId::PeriodicityOrFlow:
            st.periodicity_or_flow = std::clamp(v, 0.0, 1.0);
            break;
        case SliderId::CycleHighlight:
            st.cycle_highlight = std::clamp(v, -1000.0, 1000.0);
            break;
        case SliderId::CycleHighlightEnable:
            st.cycle_highlight_enable = std::clamp(v, 0.0, 1.0);
            break;
    }
}

static std::string format_slider_value(const AppState& st, const SliderSpec& spec) {
    const double v = get_slider_value(st, spec.id);
    if (spec.integer_mode) {
        return std::to_string(static_cast<int>(std::llround(v)));
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out << std::setprecision((std::abs(v) >= 100.0) ? 1 : 4) << v;
    return out.str();
}

static const std::vector<SliderSpec>& slider_specs() {
    static const std::vector<SliderSpec> specs = {
        {SliderId::ViewHalfWidth,      "view_half_width",      0.1,       1.0,    false, rgb(100, 180, 255)},
        {SliderId::CenterX,            "center_x",             -0.72,      0.0,   false, rgb(255, 180, 80)},
        {SliderId::CenterY,            "center_y",              0.0,      14.35,  false, rgb(255, 140, 120)},
        {SliderId::FlowDepth,          "flow_depth",           50.0,    1999.0,    true, rgb(120, 255, 120)},
        {SliderId::Curvature,          "curvature",            -0.472135955, -0.01, false, rgb(220, 120, 255)},
        {SliderId::TableOpacity,       "table_opacity",         1.0,      1.0,    false, rgb(100, 255, 180)},
        {SliderId::IslandOpacity,      "island_opacity",        0.0,      1.0,    false, rgb(60, 220, 140)},
        {SliderId::SingularityOpacity, "singularity_opacity",   0.0,      1.0,    false, rgb(255, 80, 80)},
        {SliderId::SingularityDepth,   "singularity_depth",    50.0,   2000.0,    false, rgb(255, 60, 160)},
        {SliderId::SingularityRainbow, "singularity_rainbow",   0.0,      0.0,    false, rgb(180, 255, 80)},
        {SliderId::PeriodicityOrFlow,  "periodicity_or_flow",   0.0,      1.0,    false, rgb(255, 220, 80)},
        {SliderId::CycleHighlight,     "cycle_highlight",       0.0,      0.0,    false, rgb(160, 200, 255)},
        {SliderId::CycleHighlightEnable,"cycle_highlight_enable",0.0,      0.0,   false, rgb(220, 220, 220)},
    };
    return specs;
}

static std::vector<SliderLayoutEntry> build_slider_layout(const AppState& st) {
    std::vector<SliderLayoutEntry> layout;
    const std::vector<SliderSpec>& specs = slider_specs();
    layout.reserve(specs.size());

    const int panel_w = 300;
    const int right_margin = 14;
    const int top_margin = 56;
    const int row_h = 26;
    const int rail_h = 12;
    const int x = std::max(10, st.width - panel_w - right_margin);
    const int rail_w = panel_w - 24;

    for (size_t i = 0; i < specs.size(); i++) {
        SliderSpec spec = specs[i];
        if (spec.id == SliderId::ViewHalfWidth) {
            const double a = zoom_to_view_half_width(st, BILLIARDS_ZOOM_MIN);
            const double b = zoom_to_view_half_width(st, BILLIARDS_ZOOM_MAX);
            spec.min_value = std::min(a, b);
            spec.max_value = std::max(a, b);
        }
        const int y = top_margin + static_cast<int>(i) * row_h;
        layout.push_back({spec, SliderRect{x + 12, y, rail_w, rail_h}});
    }
    return layout;
}

static int slider_index_at_pixel(const std::vector<SliderLayoutEntry>& layout, int px, int py) {
    for (size_t i = 0; i < layout.size(); i++) {
        const SliderRect& r = layout[i].rail;
        const int pad = 7;
        if (px >= r.x - pad && px <= r.x + r.w + pad && py >= r.y - pad && py <= r.y + r.h + pad) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static void update_slider_from_mouse(AppState& st, const SliderLayoutEntry& entry, int mouse_x) {
    const SliderRect& r = entry.rail;
    const double t = std::clamp((mouse_x - r.x) / static_cast<double>(std::max(1, r.w)), 0.0, 1.0);
    const double value = entry.spec.min_value * (1.0 - t) + entry.spec.max_value * t;
    set_slider_value(st, entry.spec.id, value);
}

static void draw_slider_panel(std::vector<uint32_t>& img, const AppState& st, const std::vector<SliderLayoutEntry>& layout) {
    if (!st.show_sliders || layout.empty()) return;

    const int w = st.width;
    const int h = st.height;
    const int panel_left = layout.front().rail.x - 14;
    const int panel_top = layout.front().rail.y - 30;
    const int panel_right = layout.front().rail.x + layout.front().rail.w + 14;
    const int panel_bottom = layout.back().rail.y + layout.back().rail.h + 16;

    fill_rect_alpha(img, w, h, panel_left, panel_top, panel_right - panel_left, panel_bottom - panel_top, rgb(8, 8, 8), 0.80);
    draw_line(img, w, h, panel_left, panel_top, panel_right, panel_top, rgb(255, 230, 110));
    draw_line(img, w, h, panel_left, panel_bottom, panel_right, panel_bottom, rgb(255, 230, 110));
    draw_line(img, w, h, panel_left, panel_top, panel_left, panel_bottom, rgb(255, 230, 110));
    draw_line(img, w, h, panel_right, panel_top, panel_right, panel_bottom, rgb(255, 230, 110));

    for (size_t i = 0; i < layout.size(); i++) {
        const SliderLayoutEntry& e = layout[i];
        const SliderRect& r = e.rail;
        const double value = std::clamp(get_slider_value(st, e.spec.id), e.spec.min_value, e.spec.max_value);
        const double t = (value - e.spec.min_value) / std::max(1e-12, e.spec.max_value - e.spec.min_value);
        const int handle_x = r.x + static_cast<int>(std::round(t * r.w));

        fill_rect_alpha(img, w, h, r.x, r.y, r.w, r.h, rgb(44, 44, 44), 0.92);
        fill_rect_alpha(img, w, h, r.x, r.y, std::max(0, handle_x - r.x), r.h, e.spec.color, 0.92);

        const bool active = static_cast<int>(i) == st.active_slider;
        const uint32_t hc = active ? rgb(255, 255, 255) : rgb(230, 230, 230);
        draw_circle_filled(img, w, h, handle_x, r.y + r.h / 2, active ? 7 : 6, hc);
        draw_line(img, w, h, r.x, r.y - 1, r.x + r.w, r.y - 1, rgb(210, 210, 210));
        draw_line(img, w, h, r.x, r.y + r.h, r.x + r.w, r.y + r.h, rgb(24, 24, 24));
    }
}

static Vec2 iterate_flow(const AppState& st, const Vec2& start) {
    Vec2 p = start;
    for (int i = 0; i < st.flow_depth; i++) {
        int pivot = outer_billiards_pivot(st.vertices, p, st.curvature);
        if (pivot < 0) break;
        p = outer_billiards_reflect(st.vertices[static_cast<size_t>(pivot)], p, st.curvature);
    }
    return p;
}

static std::vector<Vec2> regular_ngon_vertices(int n, double radius, double phase) {
    std::vector<Vec2> out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        const double theta = phase + static_cast<double>(i) * (2.0 * M_PI / static_cast<double>(n));
        out.emplace_back(radius * std::cos(theta), radius * std::sin(theta));
    }
    return out;
}

static std::vector<Vec2> lerp_vertices(const std::vector<Vec2>& a, const std::vector<Vec2>& b, double t) {
    const double u = std::clamp(t, 0.0, 1.0);
    const size_t n = std::max(a.size(), b.size());
    if (n == 0) return {};
    std::vector<Vec2> out(n);
    for (size_t i = 0; i < n; i++) {
        const Vec2 va = a[std::min(i, a.size() - 1)];
        const Vec2 vb = b[std::min(i, b.size() - 1)];
        out[i] = va * (1.0 - u) + vb * u;
    }
    return out;
}

static double zoom_to_view_half_width(const AppState& st, double zoom_value) {
    const double aspect = static_cast<double>(st.width) / std::max(1, st.height);
    return std::max(1e-6, 0.1 * aspect * std::exp(zoom_value));
}

static void apply_billiards_hyperbolic_script(AppState& st, double dt_sec) {
    if (!st.hyperbolic_script_active) return;
    st.hyperbolic_script_time_sec += std::max(0.0, dt_sec);

    const std::vector<Vec2> pentagon_r1 = regular_ngon_vertices(5, 1.0, BILLIARDS_PHASE);
    const std::vector<Vec2> pentagon_r3 = regular_ngon_vertices(5, 3.0, BILLIARDS_PHASE);
    const std::vector<Vec2> tri3 = regular_ngon_vertices(3, 3.0, BILLIARDS_PHASE);
    std::vector<Vec2> hack3 = {
        tri3[0],
        tri3[1],
        tri3[1],
        tri3[2],
        tri3[2],
    };
    const std::vector<Vec2> square_r3 = regular_ngon_vertices(4, 3.0, BILLIARDS_PHASE);
    const std::vector<Vec2> square_tiny = regular_ngon_vertices(4, 0.005, BILLIARDS_PHASE);

    const double t = st.hyperbolic_script_time_sec;
    st.hyperbolic_enabled = true;

    if (t < 1.0) {
        st.vertices = pentagon_r1;
        st.curvature = BILLIARDS_K_PENTAGON;
        st.view_half_width = zoom_to_view_half_width(st, 0.3);
        st.island_opacity = 1.0;
        st.singularity_opacity = 1.0;
        st.singularity_depth = std::max(st.singularity_depth, 400.0);
        st.periodicity_or_flow = 0.0;
        return;
    }

    if (t < 4.0) {
        const double u = (t - 1.0) / 3.0;
        st.vertices = lerp_vertices(pentagon_r1, pentagon_r3, u);
        const double log_a = std::log(0.472135955);
        const double log_b = std::log(0.01);
        const double curv_mag = std::exp(log_a * (1.0 - u) + log_b * u);
        st.curvature = -curv_mag;
        st.view_half_width = zoom_to_view_half_width(st, 0.3 * (1.0 - u) + (-1.5) * u);
        st.island_opacity = 1.0 - u;
        return;
    }

    if (t < 4.5) {
        const double u = (t - 4.0) / 0.5;
        st.vertices = pentagon_r3;
        st.curvature = -0.01;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.flow_depth = 1600;
        st.island_opacity = u;
        return;
    }

    if (t < 34.5) {
        st.vertices = pentagon_r3;
        st.curvature = -0.01;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.flow_depth = 1600;
        st.island_opacity = 1.0;
        return;
    }

    if (t < 36.5) {
        const double u = (t - 34.5);
        st.vertices = lerp_vertices(pentagon_r3, hack3, u);
        st.curvature = -0.01;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.flow_depth = 1600;
        st.singularity_opacity = 1.0 - u;
        return;
    }

    if (t < 40.5) {
        st.vertices = hack3;
        st.curvature = -0.01;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.flow_depth = 1600;
        st.singularity_opacity = 0.0;
        return;
    }

    if (t < 46.5) {
        const double u = (t - 40.5) / 6.0;
        st.vertices = tri3;
        st.curvature = BILLIARDS_K_TRIOCT;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.singularity_opacity = 0.4 * std::clamp(u, 0.0, 1.0);
        return;
    }

    if (t < 47.5) {
        const double u = (t - 40.5);
        st.vertices = lerp_vertices(hack3, tri3, u);
        st.curvature = BILLIARDS_K_TRIOCT;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.singularity_opacity = 0.4;
        return;
    }

    if (t < 77.5) {
        const double u = std::clamp((t - 47.5) / 30.0, 0.0, 1.0);
        st.vertices = lerp_vertices(tri3, square_r3, u);
        st.curvature = BILLIARDS_K_TRIOCT;
        st.view_half_width = zoom_to_view_half_width(st, -1.5);
        st.singularity_opacity = 0.4;
        return;
    }

    if (t < 81.5) {
        const double u = (t - 77.5) / 4.0;
        st.vertices = lerp_vertices(square_r3, square_tiny, u);
        st.curvature = BILLIARDS_K_TRIOCT * (1.0 - u) + BILLIARDS_K_END * u;
        st.view_half_width = zoom_to_view_half_width(st, (-1.5) * (1.0 - u) + (-2.5) * u);
        st.singularity_opacity = 0.4 + 0.6 * u;
        return;
    }

    st.vertices = square_tiny;
    st.curvature = BILLIARDS_K_END;
    st.view_half_width = zoom_to_view_half_width(st, -2.5);
    st.singularity_opacity = 1.0;
    st.hyperbolic_script_active = false;
}

static void render_scene(const AppState& st, std::vector<uint32_t>& img) {
    const int w = st.width;
    const int h = st.height;
    img.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

    float curvature = st.hyperbolic_enabled ? static_cast<float>(st.curvature) : 0.0f;
    int effective_depth = std::max(1, st.flow_depth / std::max(1, st.sample_stride));
    SwaptubeOuterBilliardsRenderConfig cfg;
    cfg.curvature = curvature;
    cfg.view_half_width = static_cast<float>(st.view_half_width);
    cfg.center_x = static_cast<float>(st.center_x);
    cfg.center_y = static_cast<float>(st.center_y);
    cfg.table_opacity = static_cast<float>(st.table_opacity);
    cfg.singularity_opacity = static_cast<float>(st.singularity_opacity);
    cfg.singularity_depth = static_cast<float>(st.singularity_depth);
    cfg.singularity_rainbow = static_cast<float>(st.singularity_rainbow);
    cfg.island_opacity = static_cast<float>(st.island_opacity);
    cfg.periodicity_or_flow = static_cast<float>(st.periodicity_or_flow);
    cfg.flow_depth = static_cast<float>(effective_depth);
    cfg.cycle_highlight = static_cast<float>(st.cycle_highlight);
    cfg.cycle_highlight_enable = static_cast<float>(st.cycle_highlight_enable);
    bool ok = render_frame_cuda(
        img.data(),
        w,
        h,
        st.vertices,
        cfg
    );
    if (!ok) {
        img.assign(static_cast<size_t>(w) * static_cast<size_t>(h), rgb(40, 0, 0));
    }

    if (st.show_debug_handles) {
        for (size_t i = 0; i < st.vertices.size(); i++) {
            const Vec2 a = world_to_pixel(st, st.vertices[i]);
            const Vec2 b = world_to_pixel(st, st.vertices[(i + 1) % st.vertices.size()]);
            draw_line(img, w, h,
                    static_cast<int>(std::round(a.x)), static_cast<int>(std::round(a.y)),
                    static_cast<int>(std::round(b.x)), static_cast<int>(std::round(b.y)),
                    rgb(240, 240, 240));
        }

        for (size_t i = 0; i < st.vertices.size(); i++) {
            const Vec2 p = world_to_pixel(st, st.vertices[i]);
            const uint32_t c = rgb(255, 95, 95);
            draw_circle_filled(img, w, h,
                            static_cast<int>(std::round(p.x)),
                            static_cast<int>(std::round(p.y)),
                            7, c);
        }
    }
}

static int pick_vertex(const AppState& st, int mouse_x, int mouse_y) {
    int best = -1;
    double best_d2 = 14.0 * 14.0;
    for (size_t i = 0; i < st.vertices.size(); i++) {
        Vec2 p = world_to_pixel(st, st.vertices[i]);
        double dx = p.x - mouse_x;
        double dy = p.y - mouse_y;
        double d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

static void print_controls() {
    std::cout << "Controls:\n";
    std::cout << "  Left mouse drag (on slider panel): tweak live parameters\n";
    std::cout << "  Left mouse drag: move polygon corners\n";
    std::cout << "  [ / ]: decrease/increase flow depth\n";
    std::cout << "  - / =: zoom out/in\n";
    std::cout << "  r: cycle resolution mode (fast/high/very-high)\n";
    std::cout << "  y: toggle hyperbolic mode\n";
    std::cout << "  b: play Billiards hyperbolic sequence\n";
    std::cout << "  startup: if untouched, auto-plays hyperbolic sequence\n";
    std::cout << "  any edit/click while sequence runs: stops sequence at current frame\n";
    std::cout << "  , / .: decrease/increase hyperbolic curvature magnitude\n";
    std::cout << "  j: toggle idle jitter mode (auto-start after 3s no edits)\n";
    std::cout << "  g: toggle debug vertex handles\n";
    std::cout << "  sliders panel is always visible\n";
    std::cout << "  Config file: edit live_config.txt while running\n";
    std::cout << "  1: square preset\n";
    std::cout << "  2: pentagon preset\n";
    std::cout << "  3: triangle preset\n";
    std::cout << "  h: print controls\n";
    std::cout << "  Esc or q: quit\n";
}

static std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

static bool parse_bool(const std::string& s, bool default_value) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "1" || t == "true" || t == "on" || t == "yes") return true;
    if (t == "0" || t == "false" || t == "off" || t == "no") return false;
    return default_value;
}

static void write_default_config_if_missing() {
    if (std::filesystem::exists(LIVE_CONFIG_PATH)) return;
    std::ofstream out(LIVE_CONFIG_PATH);
    out << "# live-engine runtime config (edited while app runs)\n";
    out << "flow_depth=20\n";
    out << "resolution_mode=1\n";
    out << "view_half_width=7.0\n";
    out << "center_x=0.0\n";
    out << "center_y=0.0\n";
    out << "hyperbolic_enabled=0\n";
    out << "curvature=-0.03\n";
    out << "table_opacity=1\n";
    out << "island_opacity=0\n";
    out << "singularity_opacity=0\n";
    out << "singularity_depth=0\n";
    out << "singularity_rainbow=0\n";
    out << "periodicity_or_flow=0\n";
    out << "cycle_highlight=0\n";
    out << "cycle_highlight_enable=0\n";
    out << "show_debug_handles=0\n";
    out << "idle_jitter_enabled=1\n";
    out << "idle_jitter_start_delay_sec=3.0\n";
    out << "idle_drift_speed=0.00045\n";
    out << "idle_turn_jitter=0.00014\n";
    out << "auto_play_sequence_if_idle=1\n";
    out << "auto_play_start_delay_sec=1.0\n";
}

static std::unordered_map<std::string, std::string> load_config_kv() {
    std::unordered_map<std::string, std::string> kv;
    std::ifstream in(LIVE_CONFIG_PATH);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(t.substr(0, eq));
        const std::string v = trim(t.substr(eq + 1));
        if (!k.empty()) kv[k] = v;
    }
    return kv;
}

static Vec2 polygon_centroid(const std::vector<Vec2>& verts) {
    Vec2 c(0.0, 0.0);
    if (verts.empty()) return c;
    for (const Vec2& v : verts) {
        c.x += v.x;
        c.y += v.y;
    }
    return c / static_cast<double>(verts.size());
}

static void reset_drift_state_for_vertices(const AppState& st, DriftState& drift) {
    const size_t n = st.vertices.size();
    drift.heading.assign(n, 0.0);
    drift.omega.assign(n, 0.0012);
    drift.speed.assign(n, st.idle_drift_speed * st.view_half_width);

    // Start with tangential motion around the centroid so all points initially orbit.
    const Vec2 c = polygon_centroid(st.vertices);
    for (size_t i = 0; i < n; i++) {
        const Vec2 r = st.vertices[i] - c;
        const double radial = std::atan2(r.y, r.x);
        drift.heading[i] = radial + M_PI * 0.5;
    }
}

static void apply_config_values(AppState& st, const std::unordered_map<std::string, std::string>& kv, bool& requested_resolution_reload) {
    auto get = [&](const std::string& key) -> const std::string* {
        auto it = kv.find(key);
        return it == kv.end() ? nullptr : &it->second;
    };

    if (const std::string* v = get("flow_depth")) st.flow_depth = std::clamp(std::stoi(*v), 1, 10000);
    if (const std::string* v = get("view_half_width")) st.view_half_width = std::clamp(std::stod(*v), 0.1, 200.0);
    if (const std::string* v = get("center_x")) st.center_x = std::clamp(std::stod(*v), -1e6, 1e6);
    if (const std::string* v = get("center_y")) st.center_y = std::clamp(std::stod(*v), -1e6, 1e6);
    if (const std::string* v = get("hyperbolic_enabled")) st.hyperbolic_enabled = parse_bool(*v, st.hyperbolic_enabled);
    if (const std::string* v = get("curvature")) st.curvature = std::clamp(std::stod(*v), -2.0, -1e-7);
    if (const std::string* v = get("table_opacity")) st.table_opacity = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("island_opacity")) st.island_opacity = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("singularity_opacity")) st.singularity_opacity = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("singularity_depth")) st.singularity_depth = std::clamp(std::stod(*v), 0.0, 1e6);
    if (const std::string* v = get("singularity_rainbow")) st.singularity_rainbow = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("periodicity_or_flow")) st.periodicity_or_flow = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("cycle_highlight")) st.cycle_highlight = std::stod(*v);
    if (const std::string* v = get("cycle_highlight_enable")) st.cycle_highlight_enable = std::clamp(std::stod(*v), 0.0, 1.0);
    if (const std::string* v = get("show_debug_handles")) st.show_debug_handles = parse_bool(*v, st.show_debug_handles);
    if (const std::string* v = get("idle_jitter_enabled")) st.idle_jitter_enabled = parse_bool(*v, st.idle_jitter_enabled);
    if (const std::string* v = get("idle_jitter_start_delay_sec")) st.idle_jitter_start_delay_sec = std::clamp(std::stod(*v), 0.0, 60.0);
    if (const std::string* v = get("idle_drift_speed")) st.idle_drift_speed = std::clamp(std::stod(*v), 0.0, 0.01);
    if (const std::string* v = get("idle_turn_jitter")) st.idle_turn_jitter = std::clamp(std::stod(*v), 0.0, 0.01);
    if (const std::string* v = get("auto_play_sequence_if_idle")) st.auto_play_sequence_if_idle = parse_bool(*v, st.auto_play_sequence_if_idle);
    if (const std::string* v = get("auto_play_start_delay_sec")) st.auto_play_start_delay_sec = std::clamp(std::stod(*v), 0.0, 60.0);

    if (const std::string* v = get("resolution_mode")) {
        const int old_mode = st.resolution_mode;
        apply_resolution_preset(st, std::stoi(*v));
        requested_resolution_reload = requested_resolution_reload || (st.resolution_mode != old_mode);
    }
}

static bool try_idle_drift_step(AppState& st, DriftState& drift, std::mt19937& rng, double dt_sec) {
    if (st.vertices.size() < 3) return false;
    if (drift.heading.size() != st.vertices.size()) reset_drift_state_for_vertices(st, drift);

    // Normalize timestep to 60 FPS so drift stays smooth when frame time varies.
    const double dt60 = std::clamp(dt_sec * 60.0, 0.2, 2.5);
    const double global_turn_sigma = st.idle_turn_jitter * std::sqrt(dt60) * 0.12;
    const double local_turn_sigma = global_turn_sigma * 0.35;
    std::normal_distribution<double> global_turn_noise(0.0, global_turn_sigma);
    std::normal_distribution<double> local_turn_noise(0.0, local_turn_sigma);
    const double global_turn = global_turn_noise(rng);

    std::vector<Vec2> disp(st.vertices.size(), Vec2(0.0, 0.0));
    std::vector<double> new_heading = drift.heading;
    std::vector<double> new_omega = drift.omega;
    std::vector<double> new_speed = drift.speed;

    for (size_t i = 0; i < st.vertices.size(); i++) {
        // Change direction only by a tiny amount each frame for smooth motion.
        new_omega[i] = std::clamp(new_omega[i] + global_turn + local_turn_noise(rng), -0.004, 0.004);
        new_omega[i] *= std::pow(0.998, dt60);
        new_heading[i] += new_omega[i] * dt60;

        const double target_speed = st.idle_drift_speed * st.view_half_width;
        new_speed[i] += (target_speed - new_speed[i]) * std::min(1.0, 3.0 * dt_sec);
        const double step = new_speed[i] * dt60;
        disp[i].x = std::cos(new_heading[i]) * step;
        disp[i].y = std::sin(new_heading[i]) * step;
    }

    auto try_apply_with_scale = [&](double scale) -> bool {
        auto candidate = st.vertices;
        for (size_t i = 0; i < candidate.size(); i++) {
            candidate[i].x += disp[i].x * scale;
            candidate[i].y += disp[i].y * scale;
        }
        if (!is_convex_ccw(candidate)) return false;
        st.vertices = candidate;
        return true;
    };

    if (!try_apply_with_scale(1.0) && !try_apply_with_scale(0.4) && !try_apply_with_scale(0.15)) {
        return false;
    }

    drift.heading = std::move(new_heading);
    drift.omega = std::move(new_omega);
    drift.speed = std::move(new_speed);
    return true;
}

static void apply_resolution_preset(AppState& state, int mode) {
    const int count = static_cast<int>(sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]));
    state.resolution_mode = (mode % count + count) % count;
    const ResolutionPreset& p = RES_PRESETS[state.resolution_mode];
    state.width = p.width;
    state.height = p.height;
    state.sample_stride = p.stride;
}

static XImage* create_ximage(Display* display, Visual* visual, int depth, std::vector<uint32_t>& frame, int width, int height) {
    return XCreateImage(
        display,
        visual,
        static_cast<unsigned>(depth),
        ZPixmap,
        0,
        reinterpret_cast<char*>(frame.data()),
        width,
        height,
        32,
        0
    );
}

int main() {
    if (!init_cuda_renderer()) {
        std::cerr << "Failed to initialize CUDA renderer.\n";
        return 1;
    }

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Failed to open X11 display.\n";
        shutdown_cuda_renderer();
        return 1;
    }

    const int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    AppState state;
    write_default_config_if_missing();
    DriftState drift;
    reset_drift_state_for_vertices(state, drift);

    Window window = XCreateSimpleWindow(
        display,
        root,
        50,
        50,
        static_cast<unsigned>(state.width),
        static_cast<unsigned>(state.height),
        1,
        BlackPixel(display, screen),
        BlackPixel(display, screen)
    );

    XStoreName(display, window, "Live Outer Billiards - 60 FPS");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | StructureNotifyMask);
    XMapWindow(display, window);

    Visual* visual = DefaultVisual(display, screen);
    int depth = DefaultDepth(display, screen);

    std::vector<uint32_t> frame;
    frame.resize(static_cast<size_t>(state.width) * static_cast<size_t>(state.height));

    XImage* image = create_ximage(display, visual, depth, frame, state.width, state.height);
    if (!image) {
        std::cerr << "Failed to create XImage.\n";
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        shutdown_cuda_renderer();
        return 1;
    }

    print_controls();

    bool running = true;
    const double target_fps = 60.0;
    const auto frame_dt = std::chrono::duration<double>(1.0 / target_fps);
    auto next_tick = std::chrono::steady_clock::now();

    int frame_counter = 0;
    auto fps_window_start = std::chrono::steady_clock::now();
    auto last_user_change = fps_window_start;
    auto last_config_poll = fps_window_start;
    std::filesystem::file_time_type last_config_mtime = std::filesystem::last_write_time(LIVE_CONFIG_PATH);
    std::mt19937 rng(static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    double shown_fps = 0.0;
    auto last_frame_time = std::chrono::steady_clock::now();

    while (running) {
        std::vector<SliderLayoutEntry> slider_layout = build_slider_layout(state);
        while (XPending(display) > 0) {
            XEvent ev;
            XNextEvent(display, &ev);

            if (ev.type == ConfigureNotify) {
                XConfigureEvent ce = ev.xconfigure;
                if (ce.width > 0 && ce.height > 0 && (ce.width != state.width || ce.height != state.height)) {
                    state.width = ce.width;
                    state.height = ce.height;
                    frame.resize(static_cast<size_t>(state.width) * static_cast<size_t>(state.height));

                    image->data = nullptr;
                    XDestroyImage(image);
                    image = create_ximage(display, visual, depth, frame, state.width, state.height);
                }
            } else if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button1) {
                    state.mouse_x = ev.xbutton.x;
                    state.mouse_y = ev.xbutton.y;
                    if (state.show_sliders) {
                        const int slider_idx = slider_index_at_pixel(slider_layout, ev.xbutton.x, ev.xbutton.y);
                        if (slider_idx >= 0) {
                            state.hyperbolic_script_active = false;
                            state.auto_play_consumed = true;
                            state.active_slider = slider_idx;
                            state.dragging = false;
                            state.dragged_vertex = -1;
                            update_slider_from_mouse(state, slider_layout[static_cast<size_t>(slider_idx)], ev.xbutton.x);
                            last_user_change = std::chrono::steady_clock::now();
                            continue;
                        }
                    }

                    int idx = pick_vertex(state, ev.xbutton.x, ev.xbutton.y);
                    if (idx >= 0) {
                        state.hyperbolic_script_active = false;
                        state.auto_play_consumed = true;
                        state.dragging = true;
                        state.dragged_vertex = idx;
                        last_user_change = std::chrono::steady_clock::now();
                        reset_drift_state_for_vertices(state, drift);
                    }
                }
            } else if (ev.type == ButtonRelease) {
                if (ev.xbutton.button == Button1) {
                    state.active_slider = -1;
                    state.dragging = false;
                    state.dragged_vertex = -1;
                    last_user_change = std::chrono::steady_clock::now();
                }
            } else if (ev.type == MotionNotify) {
                state.mouse_x = ev.xmotion.x;
                state.mouse_y = ev.xmotion.y;
                if (state.active_slider >= 0 && state.active_slider < static_cast<int>(slider_layout.size())) {
                    update_slider_from_mouse(state, slider_layout[static_cast<size_t>(state.active_slider)], ev.xmotion.x);
                    state.hyperbolic_script_active = false;
                    state.auto_play_consumed = true;
                    last_user_change = std::chrono::steady_clock::now();
                    continue;
                }
                if (state.dragging && state.dragged_vertex >= 0) {
                    const Vec2 wpos = pixel_to_world(state, ev.xmotion.x, ev.xmotion.y);
                    auto candidate = state.vertices;
                    candidate[static_cast<size_t>(state.dragged_vertex)] = wpos;
                    if (is_convex_ccw(candidate)) {
                        state.vertices = candidate;
                        state.hyperbolic_script_active = false;
                        state.auto_play_consumed = true;
                        last_user_change = std::chrono::steady_clock::now();
                        reset_drift_state_for_vertices(state, drift);
                    }
                }
            } else if (ev.type == KeyPress) {
                KeySym sym = XLookupKeysym(&ev.xkey, 0);
                if (sym == XK_Escape || sym == XK_q) running = false;
                else if (sym == XK_bracketleft) state.flow_depth = std::max(1, state.flow_depth - 1);
                else if (sym == XK_bracketright) state.flow_depth = std::min(200, state.flow_depth + 1);
                else if (sym == XK_minus) state.view_half_width = std::min(40.0, state.view_half_width * 1.1);
                else if (sym == XK_equal || sym == XK_plus) state.view_half_width = std::max(0.4, state.view_half_width * 0.9);
                else if (sym == XK_r) {
                    const int preset_count = static_cast<int>(sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]));
                    apply_resolution_preset(state, (state.resolution_mode + 1) % preset_count);
                    XResizeWindow(display, window, static_cast<unsigned>(state.width), static_cast<unsigned>(state.height));
                    frame.resize(static_cast<size_t>(state.width) * static_cast<size_t>(state.height));
                    image->data = nullptr;
                    XDestroyImage(image);
                    image = create_ximage(display, visual, depth, frame, state.width, state.height);
                }
                else if (sym == XK_y) {
                    state.hyperbolic_script_active = false;
                    state.hyperbolic_enabled = !state.hyperbolic_enabled;
                    if (state.hyperbolic_enabled && state.curvature >= -1e-8) state.curvature = -0.03;
                    if (!state.hyperbolic_enabled) state.curvature = 0.0;
                }
                else if (sym == XK_b) {
                    state.hyperbolic_script_active = true;
                    state.hyperbolic_script_time_sec = 0.0;
                    state.auto_play_consumed = true;
                    state.idle_jitter_active = false;
                }
                else if (sym == XK_comma) {
                    state.hyperbolic_script_active = false;
                    if (state.hyperbolic_enabled) state.curvature = std::min(-1e-5, state.curvature * 0.8);
                }
                else if (sym == XK_period) {
                    state.hyperbolic_script_active = false;
                    if (state.hyperbolic_enabled) state.curvature = std::max(-1.0, state.curvature * 1.2);
                }
                else if (sym == XK_j) {
                    state.idle_jitter_enabled = !state.idle_jitter_enabled;
                    state.idle_jitter_active = false;
                }
                else if (sym == XK_g) {
                    state.show_debug_handles = !state.show_debug_handles;
                }
                else if (sym == XK_1) {
                    state.hyperbolic_script_active = false;
                    state.vertices = {Vec2(-1.0, -1.0), Vec2(1.0, -1.0), Vec2(1.0, 1.0), Vec2(-1.0, 1.0)};
                    reset_drift_state_for_vertices(state, drift);
                } else if (sym == XK_2) {
                    state.hyperbolic_script_active = false;
                    state.vertices.clear();
                    const int n = 5;
                    for (int i = 0; i < n; i++) {
                        double theta = 1.25 * M_PI + i * (2.0 * M_PI / n);
                        state.vertices.emplace_back(2.0 * std::cos(theta), 2.0 * std::sin(theta));
                    }
                    reset_drift_state_for_vertices(state, drift);
                } else if (sym == XK_3) {
                    state.hyperbolic_script_active = false;
                    state.vertices = {
                        Vec2(-2.0, -1.5),
                        Vec2( 2.2, -0.8),
                        Vec2(-0.6,  2.2),
                    };
                    reset_drift_state_for_vertices(state, drift);
                } else if (sym == XK_h) {
                    print_controls();
                }
                if (sym != XK_h) {
                    last_user_change = std::chrono::steady_clock::now();
                    state.idle_jitter_active = false;
                    state.auto_play_consumed = true;
                }
            }
        }

        slider_layout = build_slider_layout(state);
        state.show_sliders = true;

        const auto now_for_config = std::chrono::steady_clock::now();
        if (now_for_config - last_config_poll >= std::chrono::milliseconds(250)) {
            last_config_poll = now_for_config;
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(LIVE_CONFIG_PATH, ec);
            if (!ec && mtime != last_config_mtime) {
                last_config_mtime = mtime;
                bool reload_res = false;
                try {
                    const auto kv = load_config_kv();
                    apply_config_values(state, kv, reload_res);
                    reset_drift_state_for_vertices(state, drift);
                    if (reload_res) {
                        XResizeWindow(display, window, static_cast<unsigned>(state.width), static_cast<unsigned>(state.height));
                        frame.resize(static_cast<size_t>(state.width) * static_cast<size_t>(state.height));
                        image->data = nullptr;
                        XDestroyImage(image);
                        image = create_ximage(display, visual, depth, frame, state.width, state.height);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Config parse error: " << e.what() << "\n";
                }
            }
        }

        const auto now_for_idle = std::chrono::steady_clock::now();
        const double dt_sec = std::clamp(
            std::chrono::duration_cast<std::chrono::duration<double>>(now_for_idle - last_frame_time).count(),
            1.0 / 240.0,
            0.08
        );
        last_frame_time = now_for_idle;

        if (state.auto_play_sequence_if_idle && !state.auto_play_consumed && !state.hyperbolic_script_active &&
            (now_for_idle - last_user_change) >= std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(state.auto_play_start_delay_sec))) {
            state.hyperbolic_script_active = true;
            state.hyperbolic_script_time_sec = 0.0;
            state.auto_play_consumed = true;
            state.idle_jitter_active = false;
        }

        const bool should_idle_jitter = state.idle_jitter_enabled &&
            (state.auto_play_consumed || !state.auto_play_sequence_if_idle) &&
            (now_for_idle - last_user_change) >= std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(state.idle_jitter_start_delay_sec)) &&
            !state.dragging &&
            !state.hyperbolic_script_active;
        state.idle_jitter_active = should_idle_jitter;
        if (should_idle_jitter) {
            try_idle_drift_step(state, drift, rng, dt_sec);
        }

        apply_billiards_hyperbolic_script(state, dt_sec);

        render_scene(state, frame);
        draw_slider_panel(frame, state, slider_layout);
        XPutImage(display, window, DefaultGC(display, screen), image, 0, 0, 0, 0,
                  static_cast<unsigned>(state.width), static_cast<unsigned>(state.height));

        {
            GC gc = DefaultGC(display, screen);
            XSetForeground(display, gc, 0x00ffffff);
            const std::string slider_status = "sliders: ON";
            XDrawString(display, window, gc, 12, 20, slider_status.c_str(), static_cast<int>(slider_status.size()));
        }

        if (state.show_sliders) {
            GC gc = DefaultGC(display, screen);
            XSetForeground(display, gc, 0x00ffffff);
            const int title_x = slider_layout.empty() ? 16 : slider_layout.front().rail.x;
            const int title_y = slider_layout.empty() ? 20 : slider_layout.front().rail.y - 14;
            const char* title = "Live Sliders (drag with mouse)";
            XDrawString(display, window, gc, title_x, title_y, title, static_cast<int>(std::strlen(title)));

            for (size_t i = 0; i < slider_layout.size(); i++) {
                const SliderLayoutEntry& e = slider_layout[i];
                std::string label = std::string(e.spec.label) + " = " + format_slider_value(state, e.spec);
                XDrawString(display, window, gc, e.rail.x, e.rail.y - 3, label.c_str(), static_cast<int>(label.size()));
            }
        }

        XFlush(display);

        frame_counter++;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - fps_window_start;
        if (elapsed >= std::chrono::seconds(1)) {
            shown_fps = static_cast<double>(frame_counter) /
                std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            frame_counter = 0;
            fps_window_start = now;

            std::ostringstream title;
            title.setf(std::ios::fixed);
            title.precision(1);
            const ResolutionPreset& p = RES_PRESETS[state.resolution_mode];
            title << "Live Outer Billiards - FPS " << shown_fps
                  << " | depth " << state.flow_depth
                  << " | vertices " << state.vertices.size()
                  << " | res " << state.width << "x" << state.height << " (" << p.label << ")"
                  << " | hyper " << (state.hyperbolic_enabled ? "on" : "off");
            if (state.hyperbolic_enabled) {
                title << " k=" << state.curvature;
            }
            if (state.hyperbolic_script_active) {
                title << " | hyper-script";
            }
            if (state.idle_jitter_active) {
                title << " | idle-jitter";
            }
            XStoreName(display, window, title.str().c_str());
        }

        next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_dt);
        const auto before_sleep = std::chrono::steady_clock::now();
        if (next_tick > before_sleep) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // If rendering fell behind, resync to avoid runaway lag.
            next_tick = before_sleep;
        }
    }

    image->data = nullptr;
    XDestroyImage(image);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    shutdown_cuda_renderer();
    return 0;
}
