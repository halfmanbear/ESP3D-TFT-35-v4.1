#pragma once
enum class ESP3DValuesIndex {unused};
enum class ESP3DValuesCbAction {unused};
namespace mainScreen {
void create();
bool position_values(ESP3DValuesIndex, const char*, ESP3DValuesCbAction);
bool state_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction);
bool state_comment_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction);
bool job_status_value_cb(ESP3DValuesIndex, const char*, ESP3DValuesCbAction);
}
