# Core unit tests. Built and run on Linux only (inside the build container).
set(SKU_TESTS test_basics)

foreach(t ${SKU_TESTS})
	add_executable(${t} ${t}.c)
	target_link_libraries(${t} PRIVATE sku_core)
	target_compile_options(${t} PRIVATE ${SKU_WARNINGS} -Wno-missing-prototypes)
	add_test(NAME ${t} COMMAND ${t})
endforeach()
