#include "utils.h"
#include <stdexcept>

// 解析tcp://<ip>:<port>格式的endpoint，提取ip和port
bool ParseEndpoint(const std::string &endpoint, std::string &ip, uint16_t &port)
{
    const std::string prefix = "tcp://";
    if (endpoint.find(prefix) != 0) {
        return false;
    }
    std::string addr = endpoint.substr(prefix.size());
    auto colonPos = addr.rfind(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    ip = addr.substr(0, colonPos);
    int port_temp;
    size_t port_str_len;
    try{
        port_temp = std::stoi(addr.substr(colonPos + 1), &port_str_len);
    }
    catch (const std::invalid_argument& e)
    {
        // 字符串无法转为数字
        return false;
    }
    catch (const std::out_of_range& e)
    {
        // 数值超出int范围
        return false;
    }
    // 末尾有非数字字符
    if (port_str_len != addr.substr(colonPos + 1).size()) return false;
    // 合法性检查
    if (port_temp <= 0 || port_temp > 65535) return false;
    port = static_cast<uint16_t>(port_temp);
    return !ip.empty();
}