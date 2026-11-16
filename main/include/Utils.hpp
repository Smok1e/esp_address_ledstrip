#pragma once

#include <cerrno>
#include <cstring>
#include <utility>

#include "esp_netif.h"
#include "esp_log.h"

//========================================

constexpr size_t MAX_IP4_LEN = 16;

//======================================== Network

// Parse IPv4 address from string to esp_ip4_addr sturcture
esp_err_t StrToIP4Addr(const char* str, esp_ip4_addr* addr);

// Check socket operation status
int NetCheck(int status, const char* tag, const char* expr);

#define NET_CHECK(expr) NetCheck(expr, TAG, #expr)

//======================================== Misc

void DumpHex(const void* data, size_t length);
std::pair<float, const char*> FormatBytes(size_t bytes);

//========================================