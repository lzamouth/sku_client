# Core unit tests. Built and run on Linux only (inside the build container).
set(SKU_TESTS test_basics test_protocol)

foreach(t ${SKU_TESTS})
	add_executable(${t} ${t}.c)
	target_link_libraries(${t} PRIVATE sku_core)
	target_compile_options(${t} PRIVATE ${SKU_WARNINGS} -Wno-missing-prototypes)
	add_test(NAME ${t} COMMAND ${t})
endforeach()

# HTTP test: needs a local server, run through the wrapper script
add_executable(test_http test_http.c)
target_link_libraries(test_http PRIVATE sku_core)
target_compile_options(test_http PRIVATE ${SKU_WARNINGS} -Wno-missing-prototypes)
add_test(NAME test_http COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_http_test.sh $<TARGET_FILE:test_http>)
