# sku_core: everything except the user interface. No SDL here.
find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(SKU_CORE_SOURCES
	util.c
	paths.c
	cfg.c
	crc32.c
	log.c
	strmap.c
	http.c
	protocol.c
	crc_cache.c
	worker.c
	instance.c
	scan.c
	addon.c
	addon_check.c
	addon_update.c
	report.c
	pack.c
)
if(WIN32)
	list(APPEND SKU_CORE_SOURCES platform_win32.c)
else()
	list(APPEND SKU_CORE_SOURCES platform_posix.c)
endif()

# Mozilla CA bundle embedded as a C array (mbedTLS has no system trust store)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SKU_CACERT_C ${CMAKE_CURRENT_BINARY_DIR}/cacert.c)
add_custom_command(OUTPUT ${SKU_CACERT_C}
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed.py ${CMAKE_SOURCE_DIR}/deps/dl/cacert.pem ${SKU_CACERT_C} sku_cacert_pem
	DEPENDS ${CMAKE_SOURCE_DIR}/deps/dl/cacert.pem ${CMAKE_SOURCE_DIR}/tools/embed.py
	COMMENT "Embedding cacert.pem")
list(APPEND SKU_CORE_SOURCES ${SKU_CACERT_C})

add_library(sku_core STATIC ${SKU_CORE_SOURCES})
target_include_directories(sku_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(sku_core PRIVATE ${SKU_WARNINGS})
target_compile_definitions(sku_core PUBLIC CURL_STATICLIB SKU_VERSION="${SKU_VERSION}")
if(NOT WIN32)
	target_compile_definitions(sku_core PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
endif()
target_link_libraries(sku_core PUBLIC CURL::libcurl Threads::Threads)
