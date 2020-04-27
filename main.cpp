#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>
#include <mpi.h>
#include <ospray/ospray.h>
#include <ospray/ospray_cpp.h>
#include <tbb/task_group.h>
#include <tbb/tbb.h>
#include "json.hpp"
#include "loader.h"
#include "render_backend.h"
#include "util.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

using namespace ospray;
using namespace ospcommon;
using namespace ospcommon::math;
using namespace std::chrono;
using json = nlohmann::json;

int mpi_rank = 0;
int mpi_size = 0;
box3f world_bounds;
json config;
std::string prefix;
bool use_ospray_compositing = true;
bool save_images = true;
bool detailed_cpu_stats = false;

const std::string USAGE =
    "./osp_icet <config.json> [options]\n"
    "Options:\n"
    "  -prefix <name>       Provide a prefix to prepend to the image file names.\n"
    "  -dfb                 Use OSPRay for rendering and compositing.\n"
    "  -icet                Use OSPRay for local rendering only, and IceT for compositing.\n"
    "  -no-output           Don't save images of the rendered results.\n"
    "  -detailed-stats      Record and print statistics about CPU use, thread pinning, etc.\n"
    "  -h                   Print this help.";

void render_images(const std::vector<std::string> &args);

int main(int argc, char **argv)
{
    {
        int thread_capability = 0;
        MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &thread_capability);
        if (thread_capability != MPI_THREAD_MULTIPLE) {
            std::cerr << "[error]: Thread multiple is needed for asynchronous "
                      << "rendering but is not available.\n";
            return 1;
        }
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_ARE_FATAL);

    const std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
        std::cerr << "[error]: A config file to render is required\n";
        std::cout << USAGE << "\n";
        return 1;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-prefix") {
            prefix = args[++i] + "-";
        } else if (args[i] == "-icet") {
            use_ospray_compositing = false;
        } else if (args[i] == "-dfb") {
            use_ospray_compositing = true;
        } else if (args[i] == "-no-output") {
            save_images = false;
        } else if (args[i] == "-detailed-stats") {
            detailed_cpu_stats = true;
        } else if (args[i] == "-h") {
            std::cout << USAGE << "\n";
            return 0;
        } else {
            std::ifstream cfg_file(args[i].c_str());
            if (!cfg_file) {
                std::cerr << "[error] Failed to open config file " << args[i] << "\n";
                return 1;
            }
            cfg_file >> config;
        }
    }

    if (detailed_cpu_stats) {
        char hostname[HOST_NAME_MAX + 1] = {0};
        gethostname(hostname, HOST_NAME_MAX);
        for (int i = 0; i < mpi_size; ++i) {
            if (i == mpi_rank) {
                std::cout << "rank " << mpi_rank << "/" << mpi_size << " on " << hostname
                          << "\n"
                          << get_file_content("/proc/self/status") << "\n=========\n"
                          << std::flush;
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    if (use_ospray_compositing) {
        prefix = prefix + "dfb-";
    } else {
        prefix = prefix + "icet-";
    }

    if (mpi_rank == 0) {
        std::cout << "Rendering Config: " << config.dump() << "\n" << std::flush;
        if (use_ospray_compositing) {
            std::cout << "Using OSPRay's DFB for compositing\n";
        } else {
            std::cout << "Using IceT for compositing\n";
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);

    {
        if (use_ospray_compositing) {
            ospLoadModule("mpi");
            cpp::Device device("mpiDistributed");
            device.commit();
            device.setCurrent();
        } else {
            cpp::Device device("default");
            device.commit();
            device.setCurrent();
        }

        // set an error callback to catch any OSPRay errors and exit the application
        ospDeviceSetErrorFunc(ospGetCurrentDevice(), [](OSPError error, const char *msg) {
            std::cerr << "[OSPRay error]: " << msg << std::endl << std::flush;
            std::exit(error);
        });

        render_images(args);
    }

    ospShutdown();
    MPI_Finalize();
    return 0;
}

void render_images(const std::vector<std::string> &args)
{
    VolumeBrick brick = load_volume_brick(config, mpi_rank, mpi_size);

    const vec3i volume_dims = get_vec<int, 3>(config["size"]);
    world_bounds = box3f(vec3f(0), vec3f(volume_dims));
    const vec2f value_range = get_vec<float, 2>(config["value_range"]);
    const vec2i img_size = get_vec<int, 2>(config["image_size"]);
    const auto colormap = load_colormap(config["colormap"].get<std::string>(), value_range);
    const auto camera_set = load_cameras(config["camera"].get<json>(), world_bounds);
    vec3f bg_color(0.f);
    if (config.find("bg_color") != config.end()) {
        bg_color = get_vec<float, 3>(config["bg_color"]);
    }

    std::shared_ptr<RenderBackend> backend;
    if (use_ospray_compositing) {
        backend = std::make_shared<OSPRayDFBBackend>(img_size, detailed_cpu_stats, bg_color);
    } else {
        backend =
            std::make_shared<IceTBackend>(img_size, volume_dims, detailed_cpu_stats, bg_color);
    }

    cpp::VolumetricModel model(brick.brick);
    model.setParam("transferFunction", colormap);
    model.commit();

    cpp::Group group;
    group.setParam("volume", cpp::Data(model));
    group.commit();

    cpp::Instance instance(group);
    auto transform = affine3f::translate(brick.ghost_bounds.lower);
    instance.setParam("xfm", transform);
    instance.commit();

    cpp::World world;
    world.setParam("instance", cpp::Data(instance));
    world.setParam("regions", cpp::Data(brick.bounds));
    world.commit();

    if (mpi_rank == 0) {
        std::cout << "Beginning rendering\n";
    }

    const std::string fmt_string =
        "%0" + std::to_string(static_cast<int>(std::log10(camera_set.size())) + 1) + "d";
    std::string fmt_out_buf(static_cast<int>(std::log10(camera_set.size())) + 1, '0');
    for (size_t i = 0; i < camera_set.size(); ++i) {
        cpp::Camera camera("perspective");
        camera.setParam("aspect", static_cast<float>(img_size.x) / img_size.y);
        camera.setParam("position", camera_set[i].pos);
        camera.setParam("direction", camera_set[i].dir);
        camera.setParam("up", camera_set[i].up);
        camera.commit();

        auto render_time = backend->render(camera, world, camera_set[i].pos);
        if (mpi_rank == 0) {
            std::cout << "Frame " << i << " took " << render_time << "ms\n";

            if (save_images) {
                std::string fname = prefix + "osp-icet-";
                std::sprintf(&fmt_out_buf[0], fmt_string.c_str(), static_cast<int>(i));
                fname += fmt_out_buf + ".jpg";

                const uint32_t *img = backend->map_fb();
                stbi_write_jpg(fname.c_str(), img_size.x, img_size.y, 4, img, 90);
                backend->unmap_fb(img);
            }
        }
    }
    if (mpi_rank == 0) {
        std::cout << "Rendering completed\n";
    }
}

