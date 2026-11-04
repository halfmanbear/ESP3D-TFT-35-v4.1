# Component directive
idf_component_register(
    SRCS "bsp.c" "ft6336u.c" "panel_init.c"
    INCLUDE_DIRS .
    REQUIRES esp3d_log lvgl spi_bus st7796 i2c_bus disp_backlight
)