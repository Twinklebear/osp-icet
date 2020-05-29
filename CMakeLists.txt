cmake_minimum_required(VERSION 3.5)
project(osp_icet)

if (NOT WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

find_package(ospray 2.0 REQUIRED)
find_package(IceT REQUIRED)
find_package(MPI REQUIRED)
find_package(TBB REQUIRED)

add_executable(osp_icet
    main.cpp
    util.cpp
    loader.cpp
    render_backend.cpp
    profiling.cpp)

set_target_properties(osp_icet PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON)

target_include_directories(osp_icet PUBLIC
	${ICET_INCLUDE_DIRS})

target_link_libraries(osp_icet PUBLIC
    ospray::ospray
    rkcommon::rkcommon
    MPI::MPI_CXX
    TBB::tbb
	${ICET_CORE_LIBS}
    ${ICET_MPI_LIBS})

