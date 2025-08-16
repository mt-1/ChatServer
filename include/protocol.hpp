#pragma once
#include <cstdint>
#include <string>
#include "json.hpp"
using json = nlohmann::json;

// 16字节固定头部
struct MessageHeader {
    uint32_t flag;      // 标志数 0xABABABAB
    uint32_t length;     // 消息体长度
    uint16_t msgid;      // 消息类型
    uint16_t check;    // 确认位
    uint32_t checksum;   // CRC校验
};

namespace Protocol {
    static const uint32_t FLAG_NUMBER = 0xABABABAB;
    static const uint16_t CHECK = 1;

    uint32_t crc32(const std::string& data);
    // 打包头 + body
    std::string encode(uint16_t msgid, const json& body);
    // 解析完整一帧 
    bool decodeOne(const char* data, size_t len,
                   MessageHeader& header, json& body);
}