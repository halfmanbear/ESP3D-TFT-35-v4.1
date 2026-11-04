#pragma once
enum class ESP3DClientType {serial};
struct MockCommands { ESP3DClientType getOutputClient() {return ESP3DClientType::serial;} };
inline MockCommands esp3dCommands;
