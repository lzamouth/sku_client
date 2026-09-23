add_executable(sku main.c)
target_link_libraries(sku PRIVATE sku_core)
target_compile_options(sku PRIVATE ${SKU_WARNINGS} -Wno-missing-prototypes)
