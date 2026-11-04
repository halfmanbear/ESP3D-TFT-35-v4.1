#pragma once
#include <string>
#include <vector>
struct MockSerial {
  bool available = true;
  std::vector<std::string> writes;
  bool writeRaw(const char* data, size_t size) {
    if (!available) return false;
    writes.emplace_back(data,size); return true;
  }
};
inline MockSerial serialClient;
