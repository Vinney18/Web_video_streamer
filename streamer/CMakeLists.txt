cmake_minimum_required(VERSION 3.18)
project(streamer)

set(EXE_NAME "${PROJECT_NAME}")

message("--> Project: " ${PROJECT_NAME})

# find_package(websocketpp CONFIG REQUIRED)
find_package(Poco CONFIG REQUIRED Foundation XML JSON Util Net)
find_package(Boost REQUIRED regex filesystem)
find_package(cpr CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(cryptopp CONFIG REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)

if(NOT WIN32)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
		libavformat
		libavcodec
		libswresample
		libswscale
		libavutil
		libavdevice
		libavfilter
		libpostproc
		)
endif()

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
	#message("inside msvc check checking libs")
    set(FFmpeg_Libraries "${PROJECT_SOURCE_DIR}/ffmpeg/lib/avcodec.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/avdevice.lib"	"${PROJECT_SOURCE_DIR}/ffmpeg/lib/avfilter.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/avformat.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/avutil.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/postproc.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/swresample.lib" "${PROJECT_SOURCE_DIR}/ffmpeg/lib/swscale.lib")
    #message("FFmpeg_Libraries: ", ${FFmpeg_Libraries})
else()
    #set(FFmpeg_Libraries avcodec avformat avutil swresample swscale avdevice avfilter postproc)
    set(FFmpeg_Libraries PkgConfig::LIBAV)
endif()

file(GLOB_RECURSE HEADER_FILES include/*.h include/*.hpp)
file(GLOB_RECURSE SOURCES src/*.c src/*.cpp)



add_executable (${EXE_NAME} ${SOURCES} ${HEADER_FILES})

set_target_properties(
		${EXE_NAME}
		PROPERTIES
		POSITION_INDEPENDENT_CODE ON
		CXX_STANDARD 20
		CXX_STANDARD_REQUIRED YES
		CXX_EXTENSIONS NO
		LINKER_LANGUAGE CXX
)

if (WIN32)
	target_link_libraries(
		${EXE_NAME}
		${Boost_LIBRARIES}
		Poco::Net
		Poco::Foundation
		Poco::Util
		${FFmpeg_Libraries}
		# websocketpp::websocketpp
		cpr::cpr
		jsoncpp_lib
		#jsoncpp_static
		cryptopp::cryptopp
		fmt::fmt
		spdlog::spdlog
		LibDataChannel::LibDataChannel
	)
else()
	target_link_libraries(
		${EXE_NAME}
		Boost::boost
		Boost::regex
		Boost::filesystem
		Poco::Net
		Poco::Foundation
		Poco::Util
		${FFmpeg_Libraries}
		# websocketpp::websocketpp
		cpr::cpr
		jsoncpp_static
		cryptopp::cryptopp
		fmt::fmt
		fmt::fmt-header-only
		spdlog::spdlog
		LibDataChannel::LibDataChannel
)
endif()

target_compile_definitions(${EXE_NAME} PUBLIC SPDLOG_FMT_EXTERNAL)

# Auto-copy FFmpeg DLLs next to the exe after build (Windows only)
if(WIN32)
    file(GLOB FFMPEG_DLLS "${PROJECT_SOURCE_DIR}/external_libs/av*.dll"
                          "${PROJECT_SOURCE_DIR}/external_libs/sw*.dll"
                          "${PROJECT_SOURCE_DIR}/external_libs/postproc*.dll")
    foreach(DLL ${FFMPEG_DLLS})
        add_custom_command(TARGET ${EXE_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "$<TARGET_FILE_DIR:${EXE_NAME}>"
        )
    endforeach()
endif()
