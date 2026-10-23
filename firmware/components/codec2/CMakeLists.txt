file(GLOB C2_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(SRCS ${C2_SRCS} INCLUDE_DIRS "src")
# upstream code: do not fail the firmware build on its warnings
target_compile_options(${COMPONENT_LIB} PRIVATE -w -O2)
