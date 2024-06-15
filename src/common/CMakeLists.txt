cmake_minimum_required (VERSION 3.12)
cmake_policy(SET CMP0091 NEW) # I don't remember what's this for

project(common)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE SRC_FILES 	
	RELATIVE ${PROJECT_SOURCE_DIR}
	./*.cpp;
	./*.c;
	./*.cxx;
	./*.h;
	./*.hpp;
)
#message(STATUS "${SRC_FILES}")
add_library(${PROJECT_NAME} ${SRC_FILES})
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${SRC_FILES})

set_target_properties(
	${PROJECT_NAME} PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/../bin"
	RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../bin"
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/../bin"
	RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../bin"
	RELWITHDEBINFO_OUTPUT_NAME "${PROJECT_NAME}_relwithdebinfo"
	RELEASE_OUTPUT_NAME "${PROJECT_NAME}"
	MINSIZEREL_OUTPUT_NAME "${PROJECT_NAME}_minsizerel"
	DEBUG_OUTPUT_NAME "${PROJECT_NAME}_debug"
)

target_include_directories(${PROJECT_NAME} PRIVATE 
	./../common/
	./../lib/
)
target_link_directories(${PROJECT_NAME} PRIVATE 

)
target_link_libraries(${PROJECT_NAME} 
	shlwapi.lib
	OpenGL32.lib
	hid.lib
	setupapi.lib
	xaudio2.lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE 
	_CRT_SECURE_NO_WARNINGS
	NOMINMAX
	WIN32_LEAN_AND_MEAN
)
