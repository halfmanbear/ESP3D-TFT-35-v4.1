#pragma once
enum class ESP3DScreenType {none, main};
struct MockUi { void set_current_screen(ESP3DScreenType) {} };
inline MockUi esp3dTftui;
