#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include <mpi.h>
#include <ospray/ospray.h>
#include <ospray/ospray_cpp.h>
#include <tbb/task_group.h>
#include <tbb/tbb.h>
#include "json.hpp"
#include "loader.h"
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

const std::string USAGE =
    "./osp_icet <config.json> [options]\n"
    "Options:\n"
    "  -prefix <name>       Provide a prefix to prepend to the image file names.\n"
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

    std::cout << "rank " << mpi_rank << "/" << mpi_size << "\n";

    {
        // TODO: Don't load mpi module and distributed device if we're doing local rendering
        // and compositing with IceT
        ospLoadModule("mpi");
        cpp::Device device("mpi_distributed");
        device.commit();
        device.setCurrent();

        // set an error callback to catch any OSPRay errors and exit the application
        ospDeviceSetErrorFunc(ospGetCurrentDevice(), [](OSPError error, const char *msg) {
            std::cerr << "[OSPRay error]: " << msg << std::endl << std::flush;
            std::exit(error);
        });

        render_images(std::vector<std::string>(argv, argv + argc));
    }

    ospShutdown();
    MPI_Finalize();
    return 0;
}

void render_images(const std::vector<std::string> &args)
{
    if (args.size() < 2) {
        std::cerr << "[error]: A config file to render is required\n";
        std::cout << USAGE << "\n";
        return;
    }

    json config;
    std::string prefix;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-prefix") {
            prefix = args[++i] + "-";
        } else if (args[i] == "-h") {
            std::cout << USAGE << "\n";
            return;
        } else {
            std::ifstream cfg_file(args[i].c_str());
            cfg_file >> config;
        }
    }

    if (mpi_rank == 0) {
        std::cout << "Rendering Config:\n" << config.dump(4) << "\n";
    }

    VolumeBrick brick = load_volume_brick(config, mpi_rank, mpi_size);

    world_bounds = box3f(vec3f(0), get_vec<float, 3>(config["size"]));
    const vec2f value_range = get_vec<float, 2>(config["value_range"]);
    const vec2i img_size = get_vec<int, 2>(config["image_size"]);
    const auto colormap = load_colormap(config["colormap"].get<std::string>(), value_range);
    const auto camera_set = load_cameras(config["camera"].get<json>(), world_bounds);

    // create and setup an ambient light
    cpp::Light ambient_light("ambient");
    ambient_light.commit();

    // TODO: Pick local rendering with scivis if we're using IceT compositing
    cpp::Renderer renderer("mpi_raycast");
    renderer.setParam("light", cpp::Data(ambient_light));
    if (config.find("background_color") != config.end()) {
        renderer.setParam("bgColor", get_vec<float, 3>(config["background_color"]));
    }
    renderer.setParam("volumeSamplingRate", 1.f);
    renderer.commit();

    const std::string fmt_string =
        "%0" + std::to_string(static_cast<int>(std::log10(camera_set.size())) + 1) + "d";
    std::string fmt_out_buf(static_cast<int>(std::log10(camera_set.size())) + 1, '0');

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

    cpp::FrameBuffer fb(img_size, OSP_FB_SRGBA, OSP_FB_COLOR | OSP_FB_DEPTH);
    fb.commit();

    if (mpi_rank == 0) {
        std::cout << "Beginning rendering\n";
    }

    for (size_t i = 0; i < camera_set.size(); ++i) {
        cpp::Camera camera("perspective");
        camera.setParam("aspect", static_cast<float>(img_size.x) / img_size.y);
        camera.setParam("position", camera_set[i].pos);
        camera.setParam("direction", camera_set[i].dir);
        camera.setParam("up", camera_set[i].up);
        camera.commit();

        auto start = high_resolution_clock::now();
        fb.renderFrame(renderer, camera, world);
        auto end = high_resolution_clock::now();

        if (mpi_rank == 0) {
            std::cout << "Frame " << i << " took "
                      << duration_cast<milliseconds>(end - start).count() << "ms\n";

            std::string fname = prefix + "osp-icet-";
            std::sprintf(&fmt_out_buf[0], fmt_string.c_str(), static_cast<int>(i));
            fname += fmt_out_buf + ".jpg";
            uint32_t *img = (uint32_t *)fb.map(OSP_FB_COLOR);
            stbi_write_jpg(fname.c_str(), img_size.x, img_size.y, 4, img, 90);
            fb.unmap(img);
        }
    }
    if (mpi_rank == 0) {
        std::cout << "Rendering completed\n";
    }
}

