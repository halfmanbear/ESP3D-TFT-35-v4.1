// NVS storage for the pendant preferences: one text blob under namespace "pendant".
#include "pendant_settings.h"
#include <cstring>
#include "nvs.h"

namespace pendant {
namespace {
constexpr const char* kNamespace = "pendant";
constexpr const char* kKey = "cfg";
constexpr size_t kMaxSize = 3900;  // NVS strings are limited to 4000 bytes
}
bool storageRead(std::string& text) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  size_t size = 0;
  bool ok = nvs_get_str(handle, kKey, nullptr, &size) == ESP_OK && size > 1 && size <= kMaxSize + 1;
  if (ok) {
    text.assign(size, '\0');
    ok = nvs_get_str(handle, kKey, &text[0], &size) == ESP_OK;
    text.resize(strlen(text.c_str()));
  }
  nvs_close(handle);
  return ok;
}
bool storageWrite(const std::string& text) {
  if (text.size() > kMaxSize) return false;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_str(handle, kKey, text.c_str()) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}
}
