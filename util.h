#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <ospray/ospcommon/vec.h>
#include <ospray/ospcommon/box.h>

struct VolumeBrick {
	ospcommon::vec3i id, origin, dims;
	int owner;

	VolumeBrick(const ospcommon::vec3i &brick_id, const ospcommon::vec3i &dims,
			int owner);
	float max_distance_from(const ospcommon::vec3f &p) const;

	static std::vector<VolumeBrick> compute_grid_bricks(const ospcommon::vec3i &grid,
			const ospcommon::vec3i &brick_dims);
};
std::ostream& operator<<(std::ostream &os, const VolumeBrick &b);

void write_ppm(const std::string &file_name, const int width, const int height,
		const uint32_t *img);
bool compute_divisor(int x, int &divisor);
ospcommon::vec3i compute_grid3d(int num);
ospcommon::vec2i compute_grid2d(int num);
// Hue: [0, 360], sat & val: [0, 1]
ospcommon::vec3f hsv_to_rgb(const float hue, const float sat, const float val);

