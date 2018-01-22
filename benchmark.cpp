#include <vector>
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

using namespace ospcommon;

int world_size, rank;
const vec2i img_size(1024, 1024);

void ospray_draw_callback(const double *proj_mat, const double *modelview_mat,
		const float *bg_color, const int *readback_viewport, IceTImage result);
void write_ppm(const std::string &file_name, const int width, const int height,
		const uint32_t *img);

int main(int argc, char **argv) {
	int provided = 0;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	if (ospLoadModule("mpi") != OSP_NO_ERROR) {
		throw std::runtime_error("Failed to load OSPRay MPI module");
	}

	// If we're using IceT to composite we use OSPRay in its local rendering mode
	OSPDevice device = ospNewDevice("default");
	//OSPDevice device = ospNewDevice("mpi_distributed");
	ospDeviceSet1i(device, "masterRank", 0);
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
	const vec3i volume_dims(64);
	const vec3i grid = compute_grid3d(world_size);
	const vec3i brick_id(rank % grid.x,
			(rank / grid.x) % grid.y, rank / (grid.x * grid.y));

	// We use the grid_origin to translate the bricks to the right location
	// in the space.
	const vec3f grid_origin = vec3f(brick_id) * vec3f(volume_dims);

	ospSetString(volume, "voxelType", "uchar");
	ospSetVec3i(volume, "dimensions", (osp::vec3i&)volume_dims);
	ospSetVec3f(volume, "gridOrigin", (osp::vec3f&)grid_origin);
	ospSetObject(volume, "transferFunction", transfer_fcn);

	std::vector<unsigned char> volume_data(volume_dims.x * volume_dims.y * volume_dims.z,
			static_cast<unsigned char>(rank));
	ospSetRegion(volume, volume_data.data(), osp::vec3i{0, 0, 0}, (osp::vec3i&)volume_dims);
	ospCommit(volume);

	OSPModel model = ospNewModel();
	ospAddVolume(model, volume);

	// For correct compositing we must specify a list of regions that bound the
	// data owned by this rank. These region bounds will be used for sort-last
	// compositing when rendering.
	const box3f bounds(grid_origin, grid_origin + vec3f(volume_dims));
	OSPData region_data = ospNewData(2, OSP_FLOAT3, &bounds);
	ospSetData(model, "regions", region_data);

	ospCommit(model);

	// Position the camera based on the world bounds, which go from
	// [0, 0, 0] to the upper corner of the last brick
	const vec3f world_diagonal = vec3f((world_size - 1) % grid.x,
			((world_size - 1) / grid.x) % grid.y,
			(world_size - 1) / (grid.x * grid.y))
		* vec3f(volume_dims) + vec3f(volume_dims);

	const vec3f cam_pos = world_diagonal * vec3f(1.5);
	const vec3f cam_up(0, 1, 0);
	const vec3f cam_at = world_diagonal * vec3f(0.5);
	const vec3f cam_dir = cam_at - cam_pos;

	// Setup the camera we'll render the scene from
	OSPCamera camera = ospNewCamera("perspective");
	ospSet1f(camera, "aspect", 1.0);
	ospSet3fv(camera, "pos", &cam_pos.x);
	ospSet3fv(camera, "up", &cam_up.x);
	ospSet3fv(camera, "dir", &cam_dir.x);
	ospCommit(camera);

	// For distributed rendering we must use the MPI raycaster
	OSPRenderer renderer = ospNewRenderer("mpi_raycast");
	// Setup the parameters for the renderer
	ospSet1i(renderer, "spp", 1);
	ospSet1f(renderer, "bgColor", 1.f);
	ospSetObject(renderer, "model", model);
	ospSetObject(renderer, "camera", camera);
	ospCommit(renderer);

	// Create a framebuffer to render the image too
	OSPFrameBuffer framebuffer = ospNewFrameBuffer((osp::vec2i&)img_size, OSP_FB_SRGBA,
			OSP_FB_COLOR | OSP_FB_ACCUM);
	ospFrameBufferClear(framebuffer, OSP_FB_COLOR);

	// Render the image and save it out
	//ospRenderFrame(framebuffer, renderer, OSP_FB_COLOR);

	auto icet_comm = icetCreateMPICommunicator(MPI_COMM_WORLD);
	auto icet_context = icetCreateContext(icet_comm);
	// Setup IceT for alpha-blending compositing
	icetEnable(ICET_ORDERED_COMPOSITE);
	icetEnable(ICET_CORRECT_COLORED_BACKGROUND);
	icetCompositeMode(ICET_COMPOSITE_MODE_BLEND);
	icetSetColorFormat(ICET_IMAGE_COLOR_RGBA_UBYTE);
	icetSetDepthFormat(ICET_IMAGE_DEPTH_NONE);

	// Compute the sort order for the ranks and give it to IceT
	std::vector<int> process_order;
	for (int i = 0; i < world_size; ++i) {
		process_order.push_back(i);
	}
	icetCompositeOrder(process_order.data());

	icetResetTiles();
	icetAddTile(0, 0, img_size.x, img_size.y, 0);
	icetStrategy(ICET_STRATEGY_REDUCE);

	// TODO: Tell IceT the bounding box of our volume in world space.
	// This also requires us to give it a real projection and view matrix
	// that it can use to project the box to the screen.
	// icetBoundingBoxf(bounds.lower.x, bounds.upper.x, bounds.lower.y, bounds.upper.y,
	// 					bounds.lower.z, bounds.upper.z);

	icetDrawCallback(ospray_draw_callback);
	std::array<double, 16> fake_mat = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	std::array<float, 4> icet_bgcolor = {0.1f, 0.1f, 0.1f, 0.1f};
	auto icet_img = icetDrawFrame(fake_mat.data(), fake_mat.data(), icet_bgcolor.data());

	if (rank == 0) {
		//const uint32_t *img = static_cast<const uint32_t*>(ospMapFrameBuffer(framebuffer, OSP_FB_COLOR));
		const uint32_t *img = reinterpret_cast<const uint32_t*>(icetImageGetColorcub(icet_img));
		write_ppm("regions_sample.ppm", img_size.x, img_size.y, img);
		std::cout << "Image saved to 'regions_sample.ppm'\n";
		//ospUnmapFrameBuffer(img, framebuffer);
	}

	// Clean up all our objects
	ospFreeFrameBuffer(framebuffer);
	ospRelease(renderer);
	ospRelease(camera);
	ospRelease(model);
	ospRelease(volume);

	MPI_Finalize();

	return 0;
}
void ospray_draw_callback(const double *proj_mat, const double *modelview_mat,
		const float *bg_color, const int *readback_viewport, IceTImage result)
{
	const vec2i img_grid = compute_grid2d(world_size);
	const vec2i tile_id(rank % img_grid.x, rank / img_grid.x);
	const vec2i tile_size = img_size / img_grid;
	const vec2i tile_origin = tile_id * tile_size;
	const vec3f rank_color = hsv_to_rgb((360.f * rank) / world_size, 0.8, (rank + 1.f) / world_size);

	uint8_t *img = icetImageGetColorub(result);
	for (int j = 0; j < tile_size.y; ++j) {
		const int y = j + tile_origin.y;
		for (int i = 0; i < tile_size.x; ++i) {
			const int x = i + tile_origin.x;
			uint8_t *pixel = &img[(y * img_size.x + x) * 4];
			for (int c = 0; c < 3; ++c) {
				pixel[c] = clamp(static_cast<uint8_t>(rank_color[c] * 255.f),
						uint8_t(0), uint8_t(255));
			}
			pixel[3] = 255;
		}
	}
}

