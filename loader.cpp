#include "loader.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mpi.h>
#include <ospray/ospray.h>
#include <ospray/ospray_cpp.h>
#include "json.hpp"
#include "stb_image.h"
#include "util.h"

#ifdef VTK_FOUND
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkTriangle.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnsignedShortArray.h>
#endif

Camera::Camera(const vec3f &pos, const vec3f &dir, const vec3f &up)
    : pos(pos), dir(dir), up(up)
{
}

bool compute_divisor(int x, int &divisor)
{
    const int upper = std::sqrt(x);
    for (int i = 2; i <= upper; ++i) {
        if (x % i == 0) {
            divisor = i;
            return true;
        }
    }
    return false;
}

vec3i compute_grid(int num)
{
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
std::array<int, 3> compute_ghost_faces(const vec3i &brick_id, const vec3i &grid)
{
    std::array<int, 3> faces = {NEITHER_FACE, NEITHER_FACE, NEITHER_FACE};
    for (size_t i = 0; i < 3; ++i) {
        if (brick_id[i] < grid[i] - 1) {
            faces[i] |= POS_FACE;
        }
        if (brick_id[i] > 0) {
            faces[i] |= NEG_FACE;
        }
    }
    return faces;
}

VolumeBrick load_volume_brick(json &config, const int mpi_rank, const int mpi_size)
{
    using namespace std::chrono;
    VolumeBrick brick;

    const std::string volume_file = config["volume"].get<std::string>();
    const vec3i grid = compute_grid(mpi_size);
    if (volume_file == "generated") {
        const vec3i brick_dims = get_vec<int, 3>(config["brick_size"]);
        const vec3i volume_dims = brick_dims * grid;
        config["size"] = {volume_dims.x, volume_dims.y, volume_dims.z};
    }

    const vec3i volume_dims = get_vec<int, 3>(config["size"]);
    const vec3f spacing = get_vec<int, 3>(config["spacing"]);
    const vec3i brick_id(
        mpi_rank % grid.x, (mpi_rank / grid.x) % grid.y, mpi_rank / (grid.x * grid.y));

    brick.dims = volume_dims / grid;

    const vec3f brick_lower = brick_id * brick.dims;
    const vec3f brick_upper = brick_id * brick.dims + brick.dims;

    brick.bounds = box3f(brick_lower, brick_upper);

    brick.full_dims = brick.dims;
    vec3i brick_read_offset = brick_lower;
    brick.ghost_bounds = brick.bounds;
    // Note: for this compositing benchmark we ignore the ghost zones
    // and clipping stuff, since I'm seeing some odd stuff in the scivis renderer for
    // the local rendering + IceT benchmark
#if 0
    {
        const auto ghost_faces = compute_ghost_faces(brick_id, grid);
        for (size_t i = 0; i < 3; ++i) {
            if (ghost_faces[i] & NEG_FACE) {
                brick.full_dims[i] += 1;
                brick.ghost_bounds.lower[i] -= spacing[i];
                brick_read_offset[i] -= 1;
            }
            if (ghost_faces[i] & POS_FACE) {
                brick.full_dims[i] += 1;
                brick.ghost_bounds.upper[i] += spacing[i];
            }
        }
    }
#endif
    brick.brick = cpp::Volume("structuredRegular");
    brick.brick.setParam("dimensions", brick.full_dims);
    brick.brick.setParam("gridSpacing", spacing);

    // Load the sub-bricks using MPI I/O
    size_t voxel_size = 0;
    MPI_Datatype voxel_type;
    const std::string voxel_type_string = config["type"].get<std::string>();
    if (voxel_type_string == "uint8") {
        voxel_type = MPI_UNSIGNED_CHAR;
        voxel_size = 1;
    } else if (voxel_type_string == "uint16") {
        voxel_type = MPI_UNSIGNED_SHORT;
        voxel_size = 2;
    } else if (voxel_type_string == "float32") {
        voxel_type = MPI_FLOAT;
        voxel_size = 4;
    } else if (voxel_type_string == "float64") {
        voxel_type = MPI_DOUBLE;
        voxel_size = 8;
    } else {
        throw std::runtime_error("Unrecognized voxel type " + voxel_type_string);
    }

    const size_t n_voxels =
        size_t(brick.full_dims.x) * size_t(brick.full_dims.y) * size_t(brick.full_dims.z);
    brick.voxel_data = std::make_shared<std::vector<uint8_t>>(n_voxels * voxel_size, 0);

    if (volume_file != "generated") {
        auto start = high_resolution_clock::now();
        // MPI still uses 32-bit signed ints for counts of objects, so we have to split reads
        // of large data up so the count doesn't overflow. This assumes each X-Y slice is
        // within that size limit and reads chunks
        const size_t n_chunks = n_voxels / std::numeric_limits<int32_t>::max() +
                                (n_voxels % std::numeric_limits<int32_t>::max() > 0 ? 1 : 0);

        MPI_File file_handle;
        auto rc = MPI_File_open(
            MPI_COMM_WORLD, volume_file.c_str(), MPI_MODE_RDONLY, MPI_INFO_NULL, &file_handle);
        if (rc != MPI_SUCCESS) {
            std::cerr << "[error]: Failed to open file " << volume_file
                      << ". MPI Error: " << get_mpi_error(rc) << "\n";
            throw std::runtime_error("Failed to open " + volume_file);
        }
        for (size_t i = 0; i < n_chunks; ++i) {
            const size_t chunk_thickness = brick.full_dims.z / n_chunks;
            const vec3i chunk_offset(brick_read_offset.x,
                                     brick_read_offset.y,
                                     brick_read_offset.z + i * chunk_thickness);
            vec3i chunk_dims = vec3i(brick.full_dims.x, brick.full_dims.y, chunk_thickness);
            if (i * chunk_thickness + chunk_thickness >= brick.full_dims.z) {
                chunk_dims.z = brick.full_dims.z - i * chunk_thickness;
            }
            const size_t byte_offset =
                i * chunk_thickness * brick.full_dims.y * brick.full_dims.x;
            const int chunk_voxels = chunk_dims.long_product();

            MPI_Datatype brick_type;
            MPI_Type_create_subarray(3,
                                     &volume_dims.x,
                                     &chunk_dims.x,
                                     &chunk_offset.x,
                                     MPI_ORDER_FORTRAN,
                                     voxel_type,
                                     &brick_type);
            MPI_Type_commit(&brick_type);

            MPI_File_set_view(file_handle, 0, voxel_type, brick_type, "native", MPI_INFO_NULL);
            rc = MPI_File_read_all(file_handle,
                                   brick.voxel_data->data() + byte_offset,
                                   chunk_voxels,
                                   voxel_type,
                                   MPI_STATUS_IGNORE);
            if (rc != MPI_SUCCESS) {
                std::cerr << "[error]: Failed to read all voxels from file. MPI Error: "
                          << get_mpi_error(rc) << "\n";
                throw std::runtime_error("Failed to read all voxels from file");
            }
            MPI_Type_free(&brick_type);
        }
        MPI_File_close(&file_handle);

        auto end = high_resolution_clock::now();
        if (mpi_rank == 0) {
            std::cout << "Loading volume brick took "
                      << duration_cast<milliseconds>(end - start).count() << "ms\n";
        }
    } else {
        if (voxel_type_string == "uint8") {
            std::fill(brick.voxel_data->begin(),
                      brick.voxel_data->end(),
                      static_cast<uint8_t>(mpi_rank));
        } else if (voxel_type_string == "uint16") {
            std::fill(reinterpret_cast<uint16_t *>(brick.voxel_data->data()),
                      reinterpret_cast<uint16_t *>(brick.voxel_data->data()) + n_voxels,
                      static_cast<uint16_t>(mpi_rank));
        } else if (voxel_type_string == "float32") {
            std::fill(reinterpret_cast<float *>(brick.voxel_data->data()),
                      reinterpret_cast<float *>(brick.voxel_data->data()) + n_voxels,
                      static_cast<float>(mpi_rank));
        } else if (voxel_type_string == "float64") {
            std::fill(reinterpret_cast<double *>(brick.voxel_data->data()),
                      reinterpret_cast<double *>(brick.voxel_data->data()) + n_voxels,
                      static_cast<double>(mpi_rank));
        }
    }

    cpp::SharedData osp_data;
    if (voxel_type_string == "uint8") {
        osp_data = cpp::SharedData(brick.voxel_data->data(), vec3ul(brick.full_dims));
    } else if (voxel_type_string == "uint16") {
        osp_data = cpp::SharedData(reinterpret_cast<uint16_t *>(brick.voxel_data->data()),
                                   vec3ul(brick.full_dims));
    } else if (voxel_type_string == "float32") {
        osp_data = cpp::SharedData(reinterpret_cast<float *>(brick.voxel_data->data()),
                                   vec3ul(brick.full_dims));
    } else if (voxel_type_string == "float64") {
        osp_data = cpp::SharedData(reinterpret_cast<double *>(brick.voxel_data->data()),
                                   vec3ul(brick.full_dims));
    } else {
        std::cerr << "[error]: Unsupported voxel type\n";
        throw std::runtime_error("[error]: Unsupported voxel type");
    }
    brick.brick.setParam("data", osp_data);

    // If the value range wasn't provided, compute it
    if (config.find("value_range") == config.end()) {
        vec2f value_range;
        if (volume_file != "generated") {
            auto start = high_resolution_clock::now();
            if (voxel_type == MPI_UNSIGNED_CHAR) {
                value_range = compute_value_range(brick.voxel_data->data(), n_voxels);
            } else if (voxel_type == MPI_UNSIGNED_SHORT) {
                value_range = compute_value_range(
                    reinterpret_cast<uint16_t *>(brick.voxel_data->data()), n_voxels);
            } else if (voxel_type == MPI_FLOAT) {
                value_range = compute_value_range(
                    reinterpret_cast<float *>(brick.voxel_data->data()), n_voxels);
            } else if (voxel_type == MPI_DOUBLE) {
                value_range = compute_value_range(
                    reinterpret_cast<double *>(brick.voxel_data->data()), n_voxels);
            } else {
                std::cerr << "[error]: Unsupported voxel type\n";
                throw std::runtime_error("[error]: Unsupported voxel type");
            }
            vec2f global_value_range;
            MPI_Allreduce(
                &value_range.x, &global_value_range.x, 1, MPI_FLOAT, MPI_MIN, MPI_COMM_WORLD);
            MPI_Allreduce(
                &value_range.y, &global_value_range.y, 1, MPI_FLOAT, MPI_MAX, MPI_COMM_WORLD);

            auto end = high_resolution_clock::now();

            if (mpi_rank == 0) {
                std::cout << "Computed value range: " << global_value_range << "\n"
                          << "Value range computation took "
                          << duration_cast<milliseconds>(end - start).count() << "ms\n";
            }
            config["value_range"] = {global_value_range.x, global_value_range.y};
        } else {
            config["value_range"] = {-1, mpi_size - 1};
        }
    }

    // Set the clipping box of the volume to clip off the ghost voxels
    brick.brick.setParam("volumeClippingBoxLower", brick.bounds.lower);
    brick.brick.setParam("volumeClippingBoxUpper", brick.bounds.upper);
    brick.brick.commit();
    return brick;
}

std::vector<Camera> load_cameras(const json &c, const box3f &world_bounds)
{
    std::vector<Camera> cameras;
    if (c.find("orbit") != c.end()) {
        const float orbit_radius = length(world_bounds.size()) * 0.75f;
        auto orbit_points = generate_fibonacci_sphere(c["orbit"].get<int>(), orbit_radius);
        for (const auto &p : orbit_points) {
            cameras.emplace_back(p + world_bounds.center(), normalize(-p), vec3f(0, 1, 0));
        }
    } else {
        cameras.emplace_back(get_vec<float, 3>(c["pos"]),
                             get_vec<float, 3>(c["dir"]),
                             get_vec<float, 3>(c["up"]));
    }
    return cameras;
}

cpp::TransferFunction load_colormap(const std::string &f, const vec2f &value_range)
{
    cpp::TransferFunction tfn("piecewiseLinear");
    int x, y, n;
    uint8_t *data = stbi_load(f.c_str(), &x, &y, &n, 4);
    if (!data) {
        std::cerr << "[error]: failed to load image: " << f << "\n" << std::flush;
        throw std::runtime_error("Failed to load " + f);
    }

    std::vector<vec3f> colors;
    std::vector<float> opacities;
    for (int i = 0; i < x; ++i) {
        colors.emplace_back(
            data[i * 4] / 255.f, data[i * 4 + 1] / 255.f, data[i * 4 + 2] / 255.f);
        // If no alpha in the image, generate a linear ramp
        if (n == 3) {
            opacities.emplace_back(static_cast<float>(i) / x);
        } else {
            opacities.emplace_back(data[i * 4 + 3] / 255.f);
        }
    }
    stbi_image_free(data);

    tfn.setParam("color", cpp::CopiedData(colors));
    tfn.setParam("opacity", cpp::CopiedData(opacities));
    tfn.setParam("valueRange", value_range);
    tfn.commit();
    return tfn;
}
