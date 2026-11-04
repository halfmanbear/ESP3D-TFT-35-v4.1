cmake_minimum_required(VERSION 3.16)
project(grbl_ui_test C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
file(GLOB_RECURSE LVGL_SOURCES "${REPO}/components/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC "${REPO}/components" "${REPO}/components/lvgl" "${REPO}/hardware/ESP32_ESP_TFT35_V41/components/bsp")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
file(GLOB PENDANT_SOURCES "${REPO}/main/display/cnc/grbl/pendant/*.cpp")
add_executable(grbl_ui_test preview.cpp ${PENDANT_SOURCES}
  "${REPO}/main/display/cnc/grbl/screens/main_screen.cpp"
  "${REPO}/main/target/cnc/grbl/grbl_protocol.cpp"
  "${REPO}/main/target/cnc/grbl/pendant_settings.cpp")
target_include_directories(grbl_ui_test PRIVATE stubs "${REPO}/main/target/cnc/grbl" "${REPO}/main/display/cnc/grbl")
target_compile_definitions(grbl_ui_test PRIVATE PENDANT_BSP_CONTROLS=1)
target_compile_options(grbl_ui_test PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(grbl_ui_test PRIVATE lvgl)

# Render-cost benchmark (see bench.cpp); not part of scripts/test_grbl.sh.
add_executable(grbl_ui_bench bench.cpp ${PENDANT_SOURCES}
  "${REPO}/main/display/cnc/grbl/screens/main_screen.cpp"
  "${REPO}/main/target/cnc/grbl/grbl_protocol.cpp"
  "${REPO}/main/target/cnc/grbl/pendant_settings.cpp")
target_include_directories(grbl_ui_bench PRIVATE stubs "${REPO}/main/target/cnc/grbl" "${REPO}/main/display/cnc/grbl" "${REPO}/main/display/cnc/grbl/pendant")
target_compile_definitions(grbl_ui_bench PRIVATE PENDANT_BSP_CONTROLS=1)
target_link_libraries(grbl_ui_bench PRIVATE lvgl)
