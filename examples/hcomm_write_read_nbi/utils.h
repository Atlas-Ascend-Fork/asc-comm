#include <string>
#include <cstdint>
// 解析tcp://<ip>:<port>格式的endpoint，提取ip和port
bool ParseEndpoint(const std::string &endpoint, std::string &ip, uint16_t &port);

// rank 0作为server监听，rank 1作为client连接，建立TCP通道
int32_t ConnectPeer(uint32_t rank, const std::string &ip, uint16_t port, int32_t &sock);

int32_t SendAll(int32_t sock, const void *buf, size_t len);

int32_t RecvAll(int32_t sock, void *buf, size_t len);

// TCP barrier：双方各发一个int再收一个int，确保同时到达同步点
int32_t TcpBarrier(int32_t sock);