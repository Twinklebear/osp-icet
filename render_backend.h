#pragma once

#if ICET_ENABLED
#include <IceT.h>
#include <IceTMPI.h>
#endif
#include <ospray/ospray_cpp.h>
#include "json.hpp"

using json = nlohmann::json;
using namespace ospray;
using namespace rkcommon::math;

struct RenderBackend {
    vec2i img_size;
    cpp::FrameBuffer fb;
    bool report_cpu_stats;
    int mpi_rank;
    int mpi_size;
    vec3f bg_color;

    RenderBackend(const vec2i &img_size, bool detailed_cpu_stats, const vec3f &bg_color);

    virtual ~RenderBackend() = default;

    // Render returns the total render time in milliseconds
    virtual size_t render(const cpp::Camera &camera,
                          const cpp::World &world,
                          const vec3f &cam_pos) = 0;

    virtual const uint32_t *map_fb() = 0;

    virtual void unmap_fb(const uint32_t *mapping) = 0;
};

struct OSPRayDFBBackend : RenderBackend {
    cpp::Renderer renderer;

    OSPRayDFBBackend(const vec2i &img_size, bool detailed_cpu_stats, const vec3f &bg_color);

    size_t render(const cpp::Camera &camera,
                  const cpp::World &world,
                  const vec3f &cam_pos) override;

    const uint32_t *map_fb() override;

    void unmap_fb(const uint32_t *mapping) override;
};

#if ICET_ENABLED
struct IceTBackend : RenderBackend {
    cpp::Renderer renderer;
    IceTCommunicator icet_comm;
    IceTContext icet_context;
    IceTImage icet_img;

    const cpp::World *world = nullptr;
    const cpp::Camera *camera = nullptr;

    struct BrickInfo {
        vec3i pos;
        vec3i dims;
        int owner;
        float max_distance;

        BrickInfo(const vec3i &pos, const vec3i &dims, int owner);

        BrickInfo() = default;
    };

    std::vector<BrickInfo> volume_bricks;

    IceTBackend(const vec2i &img_size,
                const vec3i &volume_dims,
                bool detailed_cpu_stats,
                const vec3f &bg_color);

    ~IceTBackend();

    size_t render(const cpp::Camera &camera,
                  const cpp::World &world,
                  const vec3f &cam_pos) override;

    const uint32_t *map_fb() override;

    void unmap_fb(const uint32_t *mapping) override;

private:
    void compute_brick_grid(const vec3i &volume_dims);

    float brick_distance(const BrickInfo &brick, const vec3f &p);

    void draw_callback(IceTImage &result);

    static void icet_draw_callback(const double *proj_mat,
                                   const double *modelview_mat,
                                   const float *bg_color,
                                   const int *readback_viewport,
                                   IceTImage result);
};
#endif
