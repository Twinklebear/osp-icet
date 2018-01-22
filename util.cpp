#include <cstdio>
#include <vector>
#include "util.h"

using namespace ospcommon;

void write_ppm(const std::string &file_name, const int width, const int height,
		const uint32_t *img)
{
	FILE *file = fopen(file_name.c_str(), "wb");
	if (!file) {
		throw std::runtime_error("Failed to open file for PPM output");
	}

	fprintf(file, "P6\n%i %i\n255\n", width, height);
	std::vector<uint8_t> out(3 * width, 0);
	for (int y = 0; y < height; ++y) {
		const uint8_t *in = reinterpret_cast<const uint8_t*>(&img[(height - 1 - y) * width]);
		for (int x = 0; x < width; ++x) {
			out[3 * x] = in[4 * x];
			out[3 * x + 1] = in[4 * x + 1];
			out[3 * x + 2] = in[4 * x + 2];
		}
		fwrite(out.data(), out.size(), sizeof(uint8_t), file);
	}
	fprintf(file, "\n");
	fclose(file);
}
bool compute_divisor(int x, int &divisor) {
	int upper_bound = std::sqrt(x);
	for (int i = 2; i <= upper_bound; ++i) {
		if (x % i == 0) {
			divisor = i;
			return true;
		}
	}
	return false;
}
vec3i compute_grid3d(int num){
	vec3i grid(1);
	int axis = 0;
	int divisor = 0;
	while (compute_divisor(num, divisor)) {
		grid[axis] *= divisor;
		num /= divisor;
		axis = (axis + 1) % 3;
	}
	if (num != 1) {
		grid[axis] *= num;
	}
	return grid;
}
vec2i compute_grid2d(int num){
	vec2i grid(1);
	int axis = 0;
	int divisor = 0;
	while (compute_divisor(num, divisor)) {
		grid[axis] *= divisor;
		num /= divisor;
		axis = (axis + 1) % 2;
	}
	if (num != 1) {
		grid[axis] *= num;
	}
	return grid;
}
vec3f hsv_to_rgb(const float hue, const float sat, const float val) {
  const float c = val * sat;
  const int h_prime = static_cast<int>(hue / 60.0);
  const float x = c * (1.0 - std::abs(h_prime % 2 - 1.0));
  vec3f rgb{0, 0, 0};
  if (h_prime >= 0 && h_prime <= 1) {
    rgb.x = c;
    rgb.y = x;
  } else if (h_prime > 1 && h_prime <= 2) {
    rgb.x = x;
    rgb.y = c;
  } else if (h_prime > 2 && h_prime <= 3) {
    rgb.y = c;
    rgb.z = x;
  } else if (h_prime > 3 && h_prime <= 4) {
    rgb.y = x;
    rgb.z = c;
  } else if (h_prime > 4 && h_prime <= 5) {
    rgb.x = x;
    rgb.z = c;
  } else if (h_prime > 5 && h_prime < 6) {
    rgb.x = c;
    rgb.z = x;
  }
  const float m = val - c;
  return rgb + vec3f(m, m, m);
}

