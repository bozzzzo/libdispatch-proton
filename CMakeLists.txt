cmake_minimum_required(VERSION 2.8)
project(Libdispatch-proton)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/qpid-proton/tools/cmake/Modules/")

add_subdirectory(qpid-proton EXCLUDE_FROM_ALL)
add_subdirectory(xdispatch EXCLUDE_FROM_ALL)

if (MSVC)
	set(CMAKE_DEBUG_POSTFIX "d")
endif (MSVC)

add_library(libdispatch_proton STATIC lp.c)
set_source_files_properties(lp.c PROPERTIES LANGUAGE CXX)
include_directories(libdispatch_proton "qpid-proton/proton-c/include" "xdispatch/libdispatch/include")
target_link_libraries(libdispatch_proton dispatch qpid-proton)

add_executable(ggg ggg.c)
set_source_files_properties(ggg.c PROPERTIES LANGUAGE CXX)
target_link_libraries(ggg libdispatch_proton)
