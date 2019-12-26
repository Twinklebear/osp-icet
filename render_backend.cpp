#include "render_backend.h"
#include <array>
#include <chrono>
#include <limits>
#include <vector>
#include <IceT.h>
#include <IceTMPI.h>
#include <mpi.h>
#include "loader.h"
#include "profiling.h"
#include "stb_image_write.h"
#include "util.h"

RenderBackend::RenderBackend(const vec2i &size, bool detailed_cpu_stats)
    : img_size(size),
      fb(size, OSP_FB_SRGBA, OSP_FB_COLOR | OSP_FB_DEPTH),
      report_cpu_stats(detailed_cpu_stats)
{
    fb.setParam("timeCompositingOverhead", 1);
    fb.commit();

    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
}

OSPRayDFBBackend::OSPRayDFBBackend(const vec2i &img_size, bool detailed_cpu_stats)
    : RenderBackend(img_size, detailed_cpu_stats), renderer("mpi_raycast")
{
    renderer.setParam("volumeSamplingRate", 1.f);
    renderer.commit();
}

size_t OSPRayDFBBackend::render(const cpp::Camera &camera,
                                const cpp::World &world,
                                const vec3f &cam_pos)
{
    using namespace std::chrono;
    ProfilingPoint start;
    fb.renderFrame(renderer, camera, world);
    ProfilingPoint end;
    if (report_cpu_stats) {
        std::cout << "rank " << mpi_rank << ", CPU: " << cpu_utilization(start, end) << "%\n";
        MPI_Barrier(MPI_COMM_WORLD);
    }
    return elapsed_time_ms(start, end);
}

const uint32_t *OSPRayDFBBackend::map_fb()
{
    return reinterpret_cast<const uint32_t *>(fb.map(OSP_FB_COLOR));
}

void OSPRayDFBBackend::unmap_fb(const uint32_t *mapping)
{
    fb.unmap(const_cast<uint32_t *>(mapping));
}

IceTBackend::BrickInfo::BrickInfo(const vec3i &pos, const vec3i &dims, int owner)
    : pos(pos), dims(dims), owner(owner)
{
}

// IceT doesn't let us send a void* through to the draw callback, so have to do some
// annoying global state
static IceTBackend *icet_backend = nullptr;

IceTBackend::IceTBackend(const vec2i &img_dims,
                         const vec3i &volume_dims,
                         bool detailed_cpu_stats)
    : RenderBackend(img_size, detailed_cpu_stats),
      renderer("scivis"),
      icet_comm(icetCreateMPICommunicator(MPI_COMM_WORLD)),
      icet_context(icetCreateContext(icet_comm)),
      icet_img(icetImageNull())
{
    fb.commit();
    renderer.setParam("volumeSamplingRate", 1.f);
    renderer.setParam("bgColor", vec4f(0.f));
    renderer.commit();

    // Setup IceT for alpha-blending compositing
    icetSingleImageStrategy(ICET_SINGLE_IMAGE_STRATEGY_AUTOMATIC);
    icetEnable(ICET_ORDERED_COMPOSITE);
    icetEnable(ICET_CORRECT_COLORED_BACKGROUND);
    icetCompositeMode(ICET_COMPOSITE_MODE_BLEND);
    icetSetColorFormat(ICET_IMAGE_COLOR_RGBA_UBYTE);
    icetSetDepthFormat(ICET_IMAGE_DEPTH_NONE);

    icetResetTiles();
    icetAddTile(0, 0, img_size.x, img_size.y, 0);
    icetStrategy(ICET_STRATEGY_REDUCE);

    icetDrawCallback(icet_draw_callback);

    compute_brick_grid(volume_dims);
}

IceTBackend::~IceTBackend()
{
    icetDestroyContext(icet_context);
}

size_t IceTBackend::render(const cpp::Camera &cam, const cpp::World &w, const vec3f &cam_pos)
{
    using namespace std::chrono;

    for (auto &b : volume_bricks) {
        b.max_distance = brick_distance(b, cam_pos);
    }

    std::sort(volume_bricks.begin(),
              volume_bricks.end(),
              [&](const BrickInfo &a, const BrickInfo &b) {
                  return a.max_distance < b.max_distance;
              });

    std::vector<int> process_order;
    std::transform(volume_bricks.begin(),
                   volume_bricks.end(),
                   std::back_inserter(process_order),
                   [](const BrickInfo &b) { return b.owner; });
    icetCompositeOrder(process_order.data());

    const std::array<double, 16> identity_mat = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::array<float, 4> icet_bgcolor = {0.0f, 0.0f, 0.0f, 0.0f};

    icet_backend = this;
    world = &w;
    camera = &cam;

    ProfilingPoint start;
    icet_img = icetDrawFrame(identity_mat.data(), identity_mat.data(), icet_bgcolor.data());
    ProfilingPoint end;

    double local_composite_time = 0;
    double local_render_time = 0;
    icetGetDoublev(ICET_COMPOSITE_TIME, &local_composite_time);
    icetGetDoublev(ICET_RENDER_TIME, &local_render_time);

    // Compositing overhead is the time between the last local rendering
    // completing and the compositing finishing, so the min time reported
    // by IceT spent in compositing
    double compositing_overhead = 0;
    MPI_Reduce(&local_composite_time,
               &compositing_overhead,
               1,
               MPI_DOUBLE,
               MPI_MIN,
               0,
               MPI_COMM_WORLD);
    if (mpi_rank == 0) {
        std::cout << "IceT Compositing Overhead: " << compositing_overhead * 1000.f << "ms\n";
    }
    if (report_cpu_stats) {
        std::cout << "rank " << mpi_rank << ", CPU: " << cpu_utilization(start, end) << "%\n";
        MPI_Barrier(MPI_COMM_WORLD);
    }
    return elapsed_time_ms(start, end);
}

const uint32_t *IceTBackend::map_fb()
{
    return reinterpret_cast<const uint32_t *>(icetImageGetColorcub(icet_img));
}

void IceTBackend::unmap_fb(const uint32_t *mapping) {}

void IceTBackend::compute_brick_grid(const vec3i &volume_dims)
{
    volume_bricks.clear();

    const vec3i grid = compute_grid(mpi_size);
    const vec3i brick_dims = volume_dims / grid;
    int owner = 0;
    for (int z = 0; z < grid.z; ++z) {
        for (int y = 0; y < grid.y; ++y) {
            for (int x = 0; x < grid.x; ++x) {
                vec3i pos = vec3i(x, y, z) * brick_dims;
                volume_bricks.emplace_back(pos, brick_dims, owner++);
            }
        }
    }
}

float IceTBackend::brick_distance(const BrickInfo &brick, const vec3f &p)
{
    // Need to check along (0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0),
    // (1, 0, 1), (0, 1, 1), (1, 1, 1) directions for each corner
    const static std::array<vec3f, 8> dirs = {vec3f(0, 0, 0),
                                              vec3f(1, 0, 0),
                                              vec3f(0, 1, 0),
                                              vec3f(0, 0, 1),
                                              vec3f(1, 1, 0),
                                              vec3f(1, 0, 1),
                                              vec3f(0, 1, 1),
                                              vec3f(1, 1, 1)};

    float max_dist = -std::numeric_limits<float>::infinity();
    for (const auto &d : dirs) {
        max_dist = std::max(length(vec3f(brick.pos) + d * vec3f(brick.dims) - p), max_dist);
    }
    return max_dist;
}

void IceTBackend::draw_callback(IceTImage &result)
{
    fb.renderFrame(renderer, *camera, *world);

    // Copy the local OSPRay rendering out to IceT
    uint8_t *img = static_cast<uint8_t *>(fb.map(OSP_FB_COLOR));
    uint8_t *output = icetImageGetColorub(result);
    std::memcpy(output, img, img_size.x * img_size.y * 4);
    fb.unmap(img);
}

void IceTBackend::icet_draw_callback(const double *proj_mat,
                                     const double *modelview_mat,
                                     const float *bg_color,
                                     const int *readback_viewport,
                                     IceTImage result)
{
    icet_backend->draw_callback(result);
}

