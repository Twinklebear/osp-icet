#pragma once

#include <cmath>
#include <string>
#include <ospray/ospcommon/vec.h>
#include <ospray/ospcommon/box.h>

void write_ppm(const std::string &file_name, const int width, const int height,
		const uint32_t *img);
bool compute_divisor(int x, int &divisor);
ospcommon::vec3i compute_grid3d(int num);
ospcommon::vec2i compute_grid2d(int num);
// Hue: [0, 360], sat & val: [0, 1]
ospcommon::vec3f hsv_to_rgb(const float hue, const float sat, const float val);

