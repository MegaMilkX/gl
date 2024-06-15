cmake_minimum_required (VERSION 3.12)
cmake_policy(SET CMP0091 NEW) # I don't remember what's this for

project(game)

set(STARTUP_PROJECT_NAME ${CMAKE_PROJECT_NAME})
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${STARTUP_PROJECT_NAME})

if (MSVC)
  add_compile_options(/bigobj)
  add_definitions(/MP)
  #add_definitions(/incremental)
  #add_definitions(/Debug:fastlink)
else ()
  add_compile_options(-Wa,-mbig-obj)
endif ()


# main project
add_subdirectory(./${CMAKE_PROJECT_NAME})
# common
add_subdirectory(./common)