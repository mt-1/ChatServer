#include "chatserver.hpp"
#include "json.hpp"

#include <string>
#include <functional>
#include "chatservice.hpp"
#include <cstring>
#include <muduo/base/Logging.h>
#include "protocol.hpp"
#include <iostream>

using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);

    // 每10秒检测一次心跳
    _loop->runEvery(10, std::bind(&ChatServer::checkHeartbeatTimeout, this));
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开连接
    if(!conn->connected())
    {
        {
            std::lock_guard<std::mutex> lk(_allConnsMtx);
            _allConns.erase(conn->name());
        }
        ChatService::instance()->clientCloseException(conn);

        conn->shutdown();
    }
    else
    {
        // 新连接加入
        // 初始化连接上下文，存储心跳时间
        conn->setContext(time(nullptr));
        std::lock_guard<std::mutex> lk(_allConnsMtx);
        _allConns[conn->name()] = conn;
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
               Buffer *buffer,
               Timestamp time)
{
    // string buf = buffer->retrieveAllAsString();
    // // 数据的反序列化
    // json js = json::parse(buf);


    // 循环解析多帧
    while (true)
    {
        // 1.头部是否完整
        if (buffer->readableBytes() < sizeof(MessageHeader))
            break;

        // 2.先检查头部（不取出）防止帧不完整
        MessageHeader hdr;
        memcpy(&hdr, buffer->peek(), sizeof(MessageHeader));


        std::cout << "parsed frame msgid=" << hdr.msgid
                  << " length=" << hdr.length
                  << " flag=" << std::hex << hdr.flag
                  << " check=" << hdr.check
                  << " checksum=" << hdr.checksum << std::endl;


        // 3.基本校验
        if (hdr.flag != Protocol::FLAG_NUMBER)
        {
            std::cerr << "protocol header invalid";
            conn->shutdown();
            return;
        }

        size_t frameLen = sizeof(MessageHeader) + hdr.length;
        // 4.是否有完整一帧
        if (buffer->readableBytes() < frameLen)
            break; // 等待更多数据

        // 5.拿出这一帧
        std::string frame(buffer->peek(), frameLen);
        buffer->retrieve(frameLen);

        // 6.解码 body
        // MessageHeader realHdr;
        json body;
        if (!Protocol::decodeOne(frame.data(), frame.size(), hdr, body))
        {
            std::cerr << "decode frame failed, close conn";
            conn->shutdown();
            return;
        }

        // 来业务了 更新活跃时间
        conn->setContext(time.secondsSinceEpoch());

        // 7.分发业务
        auto handler = ChatService::instance()->getHandler(hdr.msgid);
        handler(conn, body, time);
    }

    // // 达到的目的：完全解耦网络模块的代码和业务模块的代码
    // // 通过js["msgid"] 获取=》业务handler
    // auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    // // // 回调消息绑定好的事件处理器，来执行相应的业务处理
    // msgHandler(conn, js, time);

}

// 定时检测当前线程活跃连接
void ChatServer::checkHeartbeatTimeout()
{

    time_t now = time(nullptr);
    
    std::vector<TcpConnectionPtr> toClose;
    {
        std::lock_guard<std::mutex> lk(_allConnsMtx);
        for(auto &kv : _allConns)
        {
            const auto &c = kv.second;
            if(c->getContext().empty())
                continue;
            time_t last = any_cast<time_t>(c->getContext());
            if(now - last > _heartbeatTimeoutSecond)
            {
                // 超时，记录下来
                toClose.push_back(c);
            }
        }
    }
    for(auto &c : toClose)
    {
        LOG_INFO << "close connection due to heartbeat timeout" << c->name();
        c->shutdown();
    }
}

