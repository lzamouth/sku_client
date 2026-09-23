cmake_minimum_required(VERSION 3.16)
project(sku_spike C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)

add_executable(sku_spike main.c)
target_include_directories(sku_spike PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/src/nuklear)
target_compile_definitions(sku_spike PRIVATE CURL_STATICLIB)
target_link_directories(sku_spike PRIVATE ${SKU_DEPS_PREFIX}/lib)
target_compile_options(sku_spike PRIVATE -Wall -Wextra -Wno-unused-function -Wno-unused-parameter)
target_link_libraries(sku_spike PRIVATE SDL3::SDL3-static CURL::libcurl)

# CA bundle next to the executable (the real app will embed it)
add_custom_command(TARGET sku_spike POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../deps/dl/cacert.pem $<TARGET_FILE_DIR:sku_spike>/cacert.pem)

# osxcross has no compiler-rt: supply the @available runtime helpers ourselves
if(APPLE)
	target_sources(sku_spike PRIVATE apple_compat.c)
endif()
