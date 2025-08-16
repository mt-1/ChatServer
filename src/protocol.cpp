#include "protocol.hpp"
#include <cstring>

namespace Protocol {
uint32_t crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (unsigned char c : data) {
        crc ^= c;
        for(int i = 0;i < 8;++i)
            crc = (crc & 1)? (crc>>1) ^ 0xEDB88320 : (crc>>1);
    }
    return crc ^ 0xFFFFFFFF;
}

std::string encode(uint16_t msgid, const json& body) {
    std::string bodyStr = body.dump();
    MessageHeader h;
    h.flag = FLAG_NUMBER;
    h.length = static_cast<uint32_t>(bodyStr.size());
    h.msgid = msgid;
    h.check = CHECK;
    h.checksum = crc32(bodyStr);
    std::string out;
    out.reserve(sizeof(MessageHeader)+bodyStr.size());
    out.append(reinterpret_cast<const char*>(&h), sizeof(h));
    out.append(bodyStr);
    return out;
}

bool decodeOne(const char* data, size_t len,
               MessageHeader& header, json& body) {
    if (len < sizeof(MessageHeader)) 
        return false;
    std::memcpy(&header, data, sizeof(MessageHeader));
    if (header.flag != FLAG_NUMBER || header.check != CHECK) 
        return false;
    if (len < sizeof(MessageHeader)+header.length)
        return false;
    std::string bodyStr(data + sizeof(MessageHeader), header.length);
    if (crc32(bodyStr) != header.checksum) 
        return false;
    try 
    { 
        body = json::parse(bodyStr); 
    }
    catch(const json::parse_error&) 
    { 
        return false; 
    }
    return true;
}
}