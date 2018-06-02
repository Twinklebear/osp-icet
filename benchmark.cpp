#include <vector>
#include <chrono>
#include <cmath>
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <array>
#include <cstdio>
#include <mpi.h>
#include <ospray/ospray.h>
#include <ospray/ospcommon/vec.h>
#include <ospray/ospcommon/box.h>
#include <IceT.h>
#include <IceTMPI.h>
#include "util.h"
#include "pico_bench.h"

using namespace ospcommon;
using namespace std::chrono;

int world_size, rank;
vec2i img_size(1024, 1024);
bool use_ospray_compositing = true;
size_t benchmark_iters = 1;
// These need to be globals b/c IceT
OSPFrameBuffer framebuffer;
OSPRenderer renderer;

struct DistributedRegion {
	box3f bounds;
	int id;

	DistributedRegion(box3f bounds, int id) : bounds(bounds), id(id) {}
};

void ospray_draw_callback(const double *proj_mat, const double *modelview_mat,
		const float *bg_color, const int *readback_viewport, IceTImage result);
void write_ppm(const std::string &file_name, const int width, const int height,
		const uint32_t *img);

int main(int argc, char **argv) {
	std::vector<std::string> args{argv, argv + argc};
	if (std::find(args.begin(), args.end(), "-compositor") == args.end()) {
		std::cerr << "A compositor to benchmark is required (-compositor (ospray|icet)\n";
		return 1;
	}

	for (size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "-compositor") {
			use_ospray_compositing = args[++i] == "ospray";
		} else if (args[i] == "-img") {
			img_size.x = std::stoi(args[++i]);
			img_size.y = std::stoi(args[++i]);
		} else if (args[i] == "-n") {
			benchmark_iters = std::stoi(args[++i]);
		}
	}

	int provided = 0;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (rank == 0) {
		std::cout << "Benchmarking " << (use_ospray_compositing ? "ospray" : "icet")
			<< " compositing at " << img_size.x << "x" << img_size.y
			<< " for " << benchmark_iters << " samples" << std::endl;
	}

	ospLoadModule("ispc");
	if (ospLoadModule("mpi") != OSP_NO_ERROR) {
		throw std::runtime_error("Failed to load OSPRay MPI module");
	}

	// If we're using IceT to composite we use OSPRay in its local rendering mode
	OSPDevice device;
	if (use_ospray_compositing) {
		device = ospNewDevice("mpi_distributed");
		ospDeviceSet1i(device, "masterRank", 0);
	} else {
		device = ospNewDevice("default");
	}
	ospDeviceSetStatusFunc(device, [](const char *msg) { std::cout << msg << "\n"; });
	ospDeviceCommit(device);
	ospSetCurrentDevice(device);

	// Setup a transfer function for the volume
	OSPTransferFunction transfer_fcn = ospNewTransferFunction("piecewise_linear");
	{
		const std::vector<vec3f> colors = {
			vec3f(0, 0, 0.56),
			vec3f(0, 0, 1),
			vec3f(0, 1, 1),
			vec3f(0.5, 1, 0.5),
			vec3f(1, 1, 0),
			vec3f(1, 0, 0),
			vec3f(0.5, 0, 0)
		};
		const std::vector<float> opacities = {0.05f, 0.05f};
		OSPData colors_data = ospNewData(colors.size(), OSP_FLOAT3, colors.data());
		ospCommit(colors_data);
		OSPData opacity_data = ospNewData(opacities.size(), OSP_FLOAT, opacities.data());
		ospCommit(opacity_data);

		ospSetData(transfer_fcn, "colors", colors_data);
		ospSetData(transfer_fcn, "opacities", opacity_data);
		ospSetVec2f(transfer_fcn, "valueRange", osp::vec2f{0, static_cast<float>(world_size - 1)});
		ospCommit(transfer_fcn);
	}

	// Setup our piece of the volume data, each rank has some brick of
	// volume data within the [0, 1] box
	OSPVolume volume = ospNewVolume("block_bricked_volume");
	// TODO: Ghost zones.
	const vec3i brick_dims(64);
	const vec3i grid = compute_grid3d(world_size);
	const vec3i brick_id(rank % grid.x,
			(rank / grid.x) % grid.y, rank / (grid.x * grid.y));

	// We use the grid_origin to translate the bricks to the right location
	// in the space.
	const vec3f grid_origin = vec3f(brick_id) * vec3f(brick_dims);

	ospSetString(volume, "voxelType", "uchar");
	ospSetVec3i(volume, "dimensions", (osp::vec3i&)brick_dims);
	ospSetVec3f(volume, "gridOrigin", (osp::vec3f&)grid_origin);
	ospSetObject(volume, "transferFunction", transfer_fcn);

	std::vector<unsigned char> volume_data(brick_dims.x * brick_dims.y * brick_dims.z,
			static_cast<unsigned char>(rank));
	ospSetRegion(volume, volume_data.data(), osp::vec3i{0, 0, 0}, (osp::vec3i&)brick_dims);
	ospCommit(volume);

	OSPModel model = ospNewModel();
	ospAddVolume(model, volume);

	// For correct compositing we must specify a list of regions that bound the
	// data owned by this rank. These region bounds will be used for sort-last
	// compositing when rendering.
	const DistributedRegion region_info(box3f(grid_origin, grid_origin + vec3f(brick_dims)), rank);
	if (use_ospray_compositing) {
		OSPData region_data = ospNewData(sizeof(region_info), OSP_RAW, &region_info);
		ospSetData(model, "regions", region_data);
	}
	ospCommit(model);

	// Position the camera based on the world bounds, which go from
	// [0, 0, 0] to the upper corner of the last brick
	const vec3f world_diagonal = vec3f((world_size - 1) % grid.x,
			((world_size - 1) / grid.x) % grid.y,
			(world_size - 1) / (grid.x * grid.y))
		* vec3f(brick_dims) + vec3f(brick_dims);

	const vec3f cam_pos = world_diagonal * vec3f(1.5);
	const vec3f cam_up(0, 1, 0);
	const vec3f cam_at = world_diagonal * vec3f(0.5);
	const vec3f cam_dir = cam_at - cam_pos;
	const float cam_fovy = 60.f;

	// Setup the camera we'll render the scene from
	OSPCamera camera = ospNewCamera("perspective");
	ospSet1f(camera, "aspect", 1.0);
	ospSet3fv(camera, "pos", &cam_pos.x);
	ospSet3fv(camera, "up", &cam_up.x);
	ospSet3fv(camera, "dir", &cam_dir.x);
	ospSet1f(camera, "fovy", cam_fovy);
	ospCommit(camera);

	// For distributed rendering we must use the MPI raycaster
	renderer = ospNewRenderer("mpi_raycast");
	ospSet1f(renderer, "bgColor", 0.1f);

	// Setup the parameters for the renderer
	ospSet1i(renderer, "spp", 1);
	ospSetObject(renderer, "model", model);
	ospSetObject(renderer, "camera", camera);
	ospCommit(renderer);

	// Create a framebuffer to render the image too
	framebuffer = ospNewFrameBuffer((osp::vec2i&)img_size, OSP_FB_RGBA8,
			OSP_FB_COLOR | OSP_FB_ACCUM);
	ospFrameBufferClear(framebuffer, OSP_FB_COLOR);

	IceTImage icet_img = icetImageNull();
	pico_bench::Benchmarker<milliseconds> bencher(benchmark_iters);
	pico_bench::Statistics<milliseconds> stats({});

	IceTContext icet_context;

	// Render the image and save it out
	if (use_ospray_compositing) {
		stats = bencher([&](){
			ospRenderFrame(framebuffer, renderer, OSP_FB_COLOR);
		});
	} else {
		auto icet_comm = icetCreateMPICommunicator(MPI_COMM_WORLD);
		icet_context = icetCreateContext(icet_comm);
		// Setup IceT for alpha-blending compositing
		icetSingleImageStrategy(ICET_SINGLE_IMAGE_STRATEGY_AUTOMATIC);
		icetEnable(ICET_ORDERED_COMPOSITE);
		icetEnable(ICET_CORRECT_COLORED_BACKGROUND);
		icetCompositeMode(ICET_COMPOSITE_MODE_BLEND);
		icetSetColorFormat(ICET_IMAGE_COLOR_RGBA_UBYTE);
		icetSetDepthFormat(ICET_IMAGE_DEPTH_NONE);

		// Compute the sort order for the ranks and give it to IceT
		std::vector<VolumeBrick> volume_bricks = VolumeBrick::compute_grid_bricks(grid, brick_dims);
		std::sort(volume_bricks.begin(), volume_bricks.end(),
			[&](const VolumeBrick &a, const VolumeBrick &b) {
				return a.max_distance_from(cam_pos) < b.max_distance_from(cam_pos);
			}
		);
		std::vector<int> process_order;
		for (auto &b : volume_bricks) {
			process_order.push_back(b.owner);
		}
		icetCompositeOrder(process_order.data());

		icetResetTiles();
		icetAddTile(0, 0, img_size.x, img_size.y, 0);
		icetStrategy(ICET_STRATEGY_REDUCE);

		icetDrawCallback(ospray_draw_callback);
		std::array<double, 16> identity_mat = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
		const std::array<float, 4> icet_bgcolor = {0.1f, 0.1f, 0.1f, 0.0f};

		stats = bencher([&](){
			icet_img = icetDrawFrame(identity_mat.data(), identity_mat.data(), icet_bgcolor.data());
		});
	}

	if (rank == 0) {
		if (use_ospray_compositing) {
			std::cout << "OSPRay rendering + compositing:\n";
		} else {
			std::cout << "OSPRay rendering + IceT compositing:\n";
		}
		stats.time_suffix = "ms";
		std::cout << stats << std::endl;

		const uint32_t *img = nullptr;
		if (use_ospray_compositing) {
			img = static_cast<const uint32_t*>(ospMapFrameBuffer(framebuffer, OSP_FB_COLOR));
		} else {
			img = reinterpret_cast<const uint32_t*>(icetImageGetColorcub(icet_img));
		}

		std::string fname = use_ospray_compositing ? "ospray_" : "ospray_icet_";
		fname += std::to_string(grid.x) + "x" + std::to_string(grid.y) + "x"
			+ std::to_string(grid.z) + "-" + std::to_string(img_size.x)
			+ "x" + std::to_string(img_size.y) + ".ppm";
		write_ppm(fname, img_size.x, img_size.y, img);

		std::cout << "Image saved to '" << fname << "'" << std::endl;
		if (use_ospray_compositing) {
			ospUnmapFrameBuffer(img, framebuffer);
		}
	}

	if (!use_ospray_compositing) {
		icetDestroyContext(icet_context);
	}
	// Clean up all our objects
	ospRelease(framebuffer);
	ospRelease(renderer);
	ospRelease(camera);
	ospRelease(model);
	ospRelease(volume);
	ospShutdown();

	MPI_Finalize();

	return 0;
}
void ospray_draw_callback(const double*, const double*, const float*, const int*, IceTImage result) {
	auto startRender = high_resolution_clock::now();
	ospRenderFrame(framebuffer, renderer, OSP_FB_COLOR);
	auto endRender = high_resolution_clock::now();

	const int renderTime = duration_cast<milliseconds>(endRender - startRender).count();
	int maxTime = 0, minTime = 0;
	MPI_Reduce(&renderTime, &maxTime, 1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);
	MPI_Reduce(&renderTime, &minTime, 1, MPI_INT, MPI_MIN, 0, MPI_COMM_WORLD);
	if (rank == 0) {
		std::cout << "IceT Max OSPRay render time: " << maxTime
			<< "ms\nIceT Min OSPRay render time: " << minTime << "ms\n";
	}

	const uint8_t *img = static_cast<const uint8_t*>(ospMapFrameBuffer(framebuffer, OSP_FB_COLOR));
	uint8_t *output = icetImageGetColorub(result);
	std::memcpy(output, img, img_size.x * img_size.y * sizeof(uint32_t));
	ospUnmapFrameBuffer(img, framebuffer);
}

