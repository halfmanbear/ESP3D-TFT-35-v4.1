// Task definition for ESP32_ESP_TFT35_V41

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_TASK_CORE 0
#define NETWORK_TASK_PRIORITY 0
#define NETWORK_STACK_DEPTH 4096

#define STREAM_TASK_CORE 1
#define STREAM_TASK_PRIORITY 0
#define STREAM_STACK_DEPTH 4096

#define UI_TASK_CORE 1
/* Above the idle task (priority 0): at priority 0 the UI task could not preempt idle when an interrupt woke it, so
 * every blocking wait cost up to a 10 ms tick. Still below the serial (10) and rendering (5) tasks. */
#define UI_TASK_PRIORITY 2
#define UI_STACK_DEPTH 8192

#define STREAM_CHUNK_SIZE 1024

#define ESP3D_SOCKET_RX_BUFFER_SIZE 512
#define ESP3D_SOCKET_TASK_SIZE 4096
#define ESP3D_SOCKET_TASK_PRIORITY 5
#define ESP3D_SOCKET_TASK_CORE 0

#define ESP3D_WS_RX_BUFFER_SIZE 512
#define ESP3D_WS_TASK_SIZE 4096
#define ESP3D_WS_TASK_PRIORITY 5
#define ESP3D_WS_TASK_CORE 0

#define ESP3D_SERIAL_RX_BUFFER_SIZE 512
#define ESP3D_SERIAL_TX_BUFFER_SIZE 0
#define ESP3D_SERIAL_RX_TASK_SIZE 4096
#define ESP3D_SERIAL_TASK_CORE 1
#define ESP3D_SERIAL_TASK_PRIORITY 10

#define ESP3D_RENDERING_RX_TASK_SIZE 4096
#define ESP3D_RENDERING_TASK_PRIORITY 5
#define ESP3D_RENDERING_TASK_CORE 1

#define ESP3D_GCODE_HOST_TASK_SIZE 4096
#define ESP3D_GCODE_HOST_TASK_PRIORITY 2
#define ESP3D_GCODE_HOST_TASK_CORE 1

#define LV_TICK_PERIOD_MS 2

#ifdef __cplusplus
} /* extern "C" */
#endif