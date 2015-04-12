cmake_minimum_required (VERSION 2.8.7)

project(MCServer)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
add_definitions(-DANDROID_NDK -DEVENT__HAVE_PTHREADS)
set(CMAKE_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM} -j4")

add_library(AndroidNative
	jni/app-android.cpp
	jni/ToJava.cpp
	jni/ToJava.h
)
target_link_libraries(AndroidNative log dl)
set_target_properties(AndroidNative PROPERTIES COMPILE_DEFINITIONS "ANDROID")
include_directories(
	../lib/
	../src/
	../lib/jsoncpp/include/
	../lib/polarssl/include/
	../lib/sqlitecpp/include/
	../lib/sqlitecpp/sqlite3/
	../lib/libevent/include/
)

set(CMAKE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ ${CMAKE_CURRENT_BINARY_DIR}/MCServer)