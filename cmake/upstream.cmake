# 上游接入：openh264（Makefile）→ <build>/pkg/prefix，探针用 C++ 调编码器接口。
# 骨架顶层 project(... C)，这里补开 CXX（交叉镜像需带 riscv64-linux-gnu-g++）。

set(PKG_ROOT "${CMAKE_BINARY_DIR}/pkg")
if(NOT DEFINED PKG_PREFIX)
  set(PKG_PREFIX "${PKG_ROOT}/prefix")
  add_custom_target(pkg ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pkg-build.sh" "${SRC_DIR}" "${PKG_ROOT}"
    COMMENT "构建上游 openh264（${SRC_DIR}）"
    VERBATIM)
  set(HAVE_PKG_TARGET TRUE)
endif()

enable_language(CXX)
add_executable(h264_harness test/pkg_h264.cpp)
if(HAVE_PKG_TARGET)
  add_dependencies(h264_harness pkg)
endif()
target_include_directories(h264_harness PRIVATE "${PKG_PREFIX}/include" "${PKG_PREFIX}/include/wels")
target_link_directories(h264_harness PRIVATE "${PKG_PREFIX}/lib")
target_link_libraries(h264_harness PRIVATE openh264 pthread)
target_compile_options(h264_harness PRIVATE ${RVV_FLAGS})

add_test(NAME h264_harness COMMAND h264_harness)
