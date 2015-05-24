cmake_minimum_required (VERSION 2.6) 

include_directories(../../src/)
include_directories(../../lib/libevent/include)

# Create a single Network library that contains all the networking code:
set (Network_SRCS
	../../src/OSSupport/CriticalSection.cpp
	../../src/OSSupport/Event.cpp
	../../src/OSSupport/HostnameLookup.cpp
	../../src/OSSupport/IPLookup.cpp
	../../src/OSSupport/NetworkSingleton.cpp
	../../src/OSSupport/ServerHandleImpl.cpp
	../../src/OSSupport/TCPLinkImpl.cpp
	../../src/StringUtils.cpp
	../../src/ByteBuffer.cpp
)

set (Network_HDRS
	../../src/OSSupport/CriticalSection.h
	../../src/OSSupport/Event.h
	../../src/OSSupport/HostnameLookup.h
	../../src/OSSupport/IPLookup.h
	../../src/OSSupport/Network.h
	../../src/OSSupport/NetworkSingleton.h
	../../src/OSSupport/ServerHandleImpl.h
	../../src/OSSupport/TCPLinkImpl.h
	../../src/StringUtils.h
)

add_library(Network
	${Network_SRCS}
	${Network_HDRS}
)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11 -g")

# Set options for LibEvent, disable all their tests and benchmarks:
set(EVENT__DISABLE_OPENSSL   YES CACHE BOOL "Disable OpenSSL in LibEvent"       FORCE)
set(EVENT__DISABLE_BENCHMARK YES CACHE BOOL "Disable LibEvent benchmarks"       FORCE)
set(EVENT__DISABLE_TESTS     YES CACHE BOOL "Disable LibEvent tests"            FORCE)
set(EVENT__DISABLE_REGRESS   YES CACHE BOOL "Disable LibEvent regression tests" FORCE)
set(EVENT__DISABLE_SAMPLES   YES CACHE BOOL "Disable LibEvent samples"          FORCE)


add_subdirectory(../../lib/libevent/ ./libevent)

target_link_libraries(Network event_core event_extra)

add_executable(LoadTest main.cpp)
target_link_libraries(LoadTest Network pthread)

