cmake_minimum_required (VERSION 2.8.7)
project(MCServer)

# Tell libevent that Android has pthread (not auto detected due to NDK implementation problems)
# Tell Lua to not use unimplemented Android locale features
add_definitions(-Dgetlocaledecpoint\(\)="'.'" -DEVENT__HAVE_PTHREADS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11") # Add c++11 support here since we're not calling set_flags()
set(CMAKE_MAKE_PROGRAM "${CMAKE_MAKE_PROGRAM} -j4") # Tell make.exe to use multithreading since Android builds call make through cmake (cmake --build .)

add_library(AndroidNative
	jni/app-android.cpp
	jni/ToJava.cpp
	jni/ToJava.h
)
target_link_libraries(AndroidNative log dl)

include_directories(
	../lib/
	../src/
	../lib/jsoncpp/include/
	../lib/polarssl/include/
	../lib/sqlitecpp/include/
	../lib/sqlitecpp/sqlite3/
	../lib/libevent/include/
)

# Probably not the recommended method, but other ways don't seem to generate libraries in libs/armeabi
set(CMAKE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../")
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ ${CMAKE_CURRENT_BINARY_DIR}/MCServer)