cmake_minimum_required (VERSION 2.8.7)
project(MCServer)

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