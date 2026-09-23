# sku_core: everything except the user interface. No SDL here.
find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(SKU_CORE_SOURCES
	util.c
	paths.c
	cfg.c
	crc32.c
	log.c
	# TODO(next batch): http.c protocol.c crc_cache.c worker.c instance.c scan.c addon.c addon_check.c addon_update.c report.c
)
if(WIN32)
	list(APPEND SKU_CORE_SOURCES platform_win32.c)
else()
	list(APPEND SKU_CORE_SOURCES platform_posix.c)
endif()

add_library(sku_core STATIC ${SKU_CORE_SOURCES})
target_include_directories(sku_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(sku_core PRIVATE ${SKU_WARNINGS})
target_compile_definitions(sku_core PUBLIC CURL_STATICLIB SKU_VERSION="${SKU_VERSION}")
if(NOT WIN32)
	target_compile_definitions(sku_core PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
endif()
target_link_libraries(sku_core PUBLIC CURL::libcurl Threads::Threads)
