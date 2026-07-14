#include <string>
#include <cstdint>
// 解析tcp://<ip>:<port>格式的endpoint，提取ip和port
bool ParseEndpoint(const std::string &endpoint, std::string &ip, uint16_t &port);