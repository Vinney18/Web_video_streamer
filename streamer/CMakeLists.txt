cmake_minimum_required(VERSION 3.18)

project(streamer)

set(EXE_NAME "${PROJECT_NAME}")

message("--> Project: " ${PROJECT_NAME})

find_package(websocketpp CONFIG REQUIRED)
find_package(Poco CONFIG REQUIRED Foundation Util Net)
find_package(Boost REQUIRED regex filesystem)
find_package(cpr CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
find_package(fmt REQUIRED)


message("project source dir: ${PROJECT_SOURCE_DIR}")

include_directories(
		${PROJECT_SOURCE_DIR}/include
		${Boost_INCLUDE_DIRS}
		#${PROJECT_SOURCE_DIR}/libraries
)

if(WIN32)
    include_directories(ffmpeg/include)
endif()

if(MSVC)
    set(FFmpeg_Libraries ffmpeg/lib/avcodec ffmpeg/lib/avdevice
			ffmpeg/lib/avfilter ffmpeg/lib/avformat ffmpeg/lib/avutil ffmpeg/lib/postproc
			ffmpeg/lib/swresample ffmpeg/lib/swscale)
else()
    set(FFmpeg_Libraries avcodec avformat avutil swresample swscale) # avdevice avfilter postproc
endif()

file(GLOB_RECURSE HEADER_FILES include/*.h include/*.hpp)
file(GLOB_RECURSE SOURCES src/*.c src/*.cpp)

add_executable (${EXE_NAME} ${SOURCES} ${HEADER_FILES})

set_target_properties(
		${EXE_NAME}
		PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		CXX_STANDARD 14
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		LINKER_LANGUAGE CXX
)

target_link_libraries(
		${EXE_NAME}
		${Boost_LIBRARIES}
		Poco::Net
		Poco::Foundation
		Poco::Util
		${FFmpeg_Libraries}
		websocketpp::websocketpp
		cpr
		jsoncpp_lib
		cryptopp-static
		fmt::fmt fmt::fmt-header-only
)