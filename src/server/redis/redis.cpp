#include "redis.hpp"
#include <iostream>
using namespace std;


Redis::Redis()
    : _publish_context(nullptr), _subscribe_context(nullptr)
{
}


Redis::~Redis()
{
    if (_publish_context)
    {
        redisFree(_publish_context);
    }
    if (_subscribe_context)
    {
        redisFree(_subscribe_context);
    }
}

bool Redis::connect()
{
    // 负责publish订阅消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if(_publish_context == nullptr)
    {
        cerr << "Redis publish context connect error!" << endl;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subscribe_context = redisConnect("127.0.1", 6379);
    if(_subscribe_context == nullptr)
    {
        cerr << "Redis subscribe context connect error!" << endl;
        return false;
    }

    thread t([&]{
        observer_channel_message();
    });

    t.detach(); // 分离线程，独立运行
    cout << "Redis connect success!" << endl;
    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (reply == nullptr)
    {
        cerr << "Redis publish error!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel)
{
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做了订阅通道，不接受通道消息
    // 通道消息的接受专门在observer_channel_message()函数中的独立线程处理
    // 只负责发送命令，不阻塞接受redis server响应消息，否则和notifyMsg线程抢占响应资源
    if(REDIS_ERR == redisAppendCommand(_subscribe_context, "SUBSCRIBE %d", channel))
    {
        cerr << "Redis subscribe error!" << endl;
        return false;
    }
    // redisBufferWriter可以循环发送缓冲区，知道缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while(!done)
    {
        if(REDIS_ERR == redisBufferWrite(this->_subscribe_context, &done))
        {
            cerr << "subscribe redisBufferWrite error!" << endl;
            return false;
        }
    }

    return true;
}


// 向redis指定的通道unsubscribe订阅消息
bool Redis::unsubscribe(int channel)
{
    if(REDIS_ERR == redisAppendCommand(_subscribe_context, "UNSUBSCRIBE %d", channel))
    {
        cerr << "Redis unsubscribe error!" << endl;
        return false;
    }
    // redisBufferWriter可以循环发送缓冲区，知道缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while(!done)
    {
        if(REDIS_ERR == redisBufferWrite(this->_subscribe_context, &done))
        {
            cerr << "unsubscribe redisBufferWrite error!" << endl;
            return false;
        }
    }

    return true;
}

void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;
    while(REDIS_OK == redisGetReply(this->_subscribe_context, (void **)&reply))
    {
        // 订阅收到的消息格式为：["subscribe", "channel", "message"]
        if(reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            // 给业务层上报通道上发生的消息
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        }

        freeReplyObject(reply);
    }

    cerr << ">>>>>>>>>> observer_channel_message exit! <<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}
