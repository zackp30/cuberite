
cmake_minimum_required (VERSION 2.6)
project (MCServer)

include_directories ("${PROJECT_SOURCE_DIR}/../")

if(USE_OPENCL_CPU OR USE_OPENCL_GPU)
	set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/lib/")
	include(FindOpenCl)
	include_directories(${OPENCL_INCLUDE_DIR})
endif()

file(GLOB SOURCE
    "*.cpp"
    "*.h"
	"*.cl"
)

add_library(Generating ${SOURCE})

target_link_libraries(Generating OSSupport iniFile Blocks)

if(USE_OPENCL_CPU OR USE_OPENCL_GPU)
	target_link_libraries(Generating ${OPENCL_LIBRARY})
endif()
