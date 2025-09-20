#ifndef CHATSERVER_H
#define CHATSERVER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>
#include <unordered_map>
#include <memory>
#include "json.hpp"
#include <mutex>
using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;



// 聊天服务器的主类
class ChatServer
{
public:
    // 初始化聊天服务器对象
    ChatServer(EventLoop *loop,
               const InetAddress &listenAddr,
               const string &nameArg);

    // 启动服务
    void start();

private:
    // 上报链接相关信息的回调函数
    void onConnection(const TcpConnectionPtr &);

    // 上报读写事件相关信息的回调函数
    void onMessage(const TcpConnectionPtr &,
                   Buffer *,
                   Timestamp);

    
    // 检查心跳超时
    void checkHeartbeatTimeout();
    // 超时阈值
    int _heartbeatTimeoutSecond = 200;

    // 所有连接
    std::mutex _allConnsMtx;
    // key: conn->name
    std::unordered_map<std::string, TcpConnectionPtr> _allConns;
    
    TcpServer _server;  // 组合的muduo库，实现服务器功能的类对象
    EventLoop *_loop;   // 指向事件循环对象的指针



};


#endif