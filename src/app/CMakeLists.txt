# Graphical client: SDL3 + Nuklear on top of sku_core.
find_package(SDL3 CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# embedded assets: fonts and the third-party notices
set(SKU_EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embed)
file(MAKE_DIRECTORY ${SKU_EMBED_DIR})
function(sku_embed input symbol)
	set(out ${SKU_EMBED_DIR}/${symbol}.c)
	add_custom_command(OUTPUT ${out}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed.py ${input} ${out} ${symbol}
		DEPENDS ${input} ${CMAKE_SOURCE_DIR}/tools/embed.py
		COMMENT "Embedding ${symbol}")
	set(SKU_EMBEDDED ${SKU_EMBEDDED} ${out} PARENT_SCOPE)
endfunction()
sku_embed(${CMAKE_SOURCE_DIR}/deps/src/fonts/ttf/DejaVuSans.ttf     sku_font_sans)
sku_embed(${CMAKE_SOURCE_DIR}/deps/src/fonts/ttf/DejaVuSansMono.ttf sku_font_mono)
sku_embed(${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt                 sku_notices)

set(SKU_APP_SOURCES main.c prefs.c jobs.c theme.c widgets.c ui_list.c ui_detail.c ui_pack.c ui_options.c ${SKU_EMBEDDED})
if(APPLE)
	list(APPEND SKU_APP_SOURCES apple_compat.c)   # osxcross has no compiler-rt (@available helpers)
endif()
if(WIN32)
	# icon + version information resource
	enable_language(RC)
	set(SKU_ICON_ICO ${CMAKE_SOURCE_DIR}/packaging/icons/icon.ico)
	configure_file(${CMAKE_SOURCE_DIR}/packaging/windows/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
	list(APPEND SKU_APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

if(WIN32)
	set(SKU_APP_NAME SkunkcraftsUpdater)
elseif(APPLE)
	set(SKU_APP_NAME SkunkcraftsUpdater)
else()
	set(SKU_APP_NAME SkunkcraftsUpdater_lin)
endif()

add_executable(sku_app ${SKU_APP_SOURCES})
set_target_properties(sku_app PROPERTIES OUTPUT_NAME ${SKU_APP_NAME})
target_include_directories(sku_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/deps/src/nuklear)
target_compile_options(sku_app PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-unused-function -Wno-unused-but-set-variable)
target_link_libraries(sku_app PRIVATE sku_core SDL3::SDL3-static)
if(WIN32)
	target_link_options(sku_app PRIVATE -mwindows)   # GUI subsystem: no console window
endif()
