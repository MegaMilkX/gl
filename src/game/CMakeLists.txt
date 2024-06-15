cmake_minimum_required (VERSION 3.12)
cmake_policy(SET CMP0091 NEW) # I don't remember what's this for

project(game)

set(DEBUG_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/../data")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (NOT EXISTS ${DEBUG_WORKING_DIRECTORY})
	file(MAKE_DIRECTORY ${DEBUG_WORKING_DIRECTORY})
	file(WRITE "${DEBUG_WORKING_DIRECTORY}/ReadMe.txt" "This is your debug working directory")
endif ()

file(GLOB_RECURSE SRC_FILES 	
	RELATIVE ${PROJECT_SOURCE_DIR}
	./*.cpp;
	./*.c;
	./*.cxx;
	./*.h;
	./*.hpp;
)
#message(STATUS "${SRC_FILES}")
if (MSVC)
	add_executable(${PROJECT_NAME} ${SRC_FILES} "${CMAKE_SOURCE_DIR}../../build/data/resource.rc")
else ()
	add_executable(${PROJECT_NAME} ${SRC_FILES})
endif ()
source_group(TREE ${PROJECT_SOURCE_DIR} FILES ${SRC_FILES})

set_target_properties(
	${PROJECT_NAME} PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY ${DEBUG_WORKING_DIRECTORY}
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
	./../game/
	./../lib/
	./../common/
)
target_link_directories(${PROJECT_NAME} PRIVATE 

)
target_link_libraries(${PROJECT_NAME} 
	shlwapi.lib
	OpenGL32.lib
	hid.lib
	setupapi.lib
	xaudio2.lib
	common
)

target_compile_definitions(${PROJECT_NAME} PRIVATE 
	_CRT_SECURE_NO_WARNINGS
	NOMINMAX
	WIN32_LEAN_AND_MEAN
)
