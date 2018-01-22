cmake_minimum_required(VERSION 3.5)
project(osp_icet)

set(CMAKE_CXX_STANDARD 14)

find_package(ospray REQUIRED)
find_package(IceT REQUIRED)
find_package(MPI REQUIRED)
include_directories(${OSPRAY_INCLUDE_DIRS} ${MPI_CXX_INCLUDE_PATH}
	${ICET_INCLUDE_DIRS})

add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark ${OSPRAY_LIBRARIES} ${MPI_CXX_LIBRARIES}
	${ICET_CORE_LIBS} ${ICET_MPI_LIBS})


