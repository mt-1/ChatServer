#include "redis.hpp"
#include <iostream>
using namespace std;


Redis::Redis()
    : _context_mutexes(POOL_SIZE)
{
}


Redis::~Redis() 
{
    // 清理发布连接
    for(size_t i = 0; i < _publish_contexts.size(); i++) {
        std::lock_guard<std::mutex> lock(_context_mutexes[i]);
        if(_publish_contexts[i]) {
            redisFree(_publish_contexts[i]);
            _publish_contexts[i] = nullptr;
        }
    }
    
    // 清理订阅连接
    if (_subscribe_context) {
        redisFree(_subscribe_context);
        _subscribe_context = nullptr;
    }
    

}


bool Redis::connect() {
    
    // 清理旧连接
    for(size_t i = 0; i < _publish_contexts.size(); i++) {
        std::lock_guard<std::mutex> lock(_context_mutexes[i]);
        if(_publish_contexts[i]) {
            redisFree(_publish_contexts[i]);
            _publish_contexts[i] = nullptr;
        }
    }
    _publish_contexts.clear();
    _publish_contexts.resize(POOL_SIZE, nullptr);
    
    // 创建连接池
    for (int i = 0; i < POOL_SIZE; i++) {
        std::lock_guard<std::mutex> lock(_context_mutexes[i]);
        
        redisContext* ctx = redisConnect("127.0.0.1", 6379);
        if (ctx == nullptr || ctx->err) {
            // 清理已创建的连接
            for(int j = 0; j < i; j++) {
                std::lock_guard<std::mutex> cleanupLock(_context_mutexes[j]);
                if(_publish_contexts[j]) {
                    redisFree(_publish_contexts[j]);
                    _publish_contexts[j] = nullptr;
                }
            }
            return false;
        }
        
        _publish_contexts[i] = ctx;

    }
    
    // 创建订阅连接
    _subscribe_context = redisConnect("127.0.0.1", 6379);
    if(_subscribe_context == nullptr || _subscribe_context->err) {
        return false;
    }
    
    // 启动观察者线程
    thread t([this] { observer_channel_message(); });
    t.detach();

    return true;
}

// bool Redis::connect()
// {
//     // 负责publish订阅消息的上下文连接
//     _publish_context = redisConnect("127.0.0.1", 6379);
//     if(_publish_context == nullptr)
//     {
//         cerr << "Redis publish context connect error!" << endl;
//         return false;
//     }

//     // 负责subscribe订阅消息的上下文连接
//     _subscribe_context = redisConnect("127.0.0.1", 6379);
//     if(_subscribe_context == nullptr)
//     {
//         cerr << "Redis subscribe context connect error!" << endl;
//         return false;
//     }

//     thread t([&]{
//         observer_channel_message();
//     });

//     t.detach(); // 分离线程，独立运行
//     cout << "Redis connect success!" << endl;
//     return true;
// }



bool Redis::publish(int channel, string message) {
    if(_publish_contexts.empty()) {
        cerr << "Redis无连接" << endl;
        return false;
    }
    
    // 轮询选择连接
    int index = _current_index.fetch_add(1) % POOL_SIZE;
    
    // 锁定特定连接
    std::lock_guard<std::mutex> lock(_context_mutexes[index]);
    redisContext* rc = _publish_contexts[index];
    
    if (rc->err) {
        // 重连
        redisFree(rc);
        rc = redisConnect("127.0.0.1", 6379);
        if (rc == nullptr || rc->err) {
            cerr << "Redis连接失败" << index << endl;
            _publish_contexts[index] = nullptr;
            return false;
        }
        _publish_contexts[index] = rc;
    }
    
    // 执行发布命令
    redisReply* reply = (redisReply*)redisCommand(rc, "PUBLISH %d %s", channel, message.c_str());
    
    freeReplyObject(reply);
    
    return true;
}


// 向redis指定的通道channel发布消息
// bool Redis::publish(int channel, string message)
// {
//     redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
//     if (reply == nullptr)
//     {
//         cerr << "Redis publish error!" << endl;
//         return false;
//     }
//     freeReplyObject(reply);
//     return true;
// }

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
    int retry = 0;
    const int maxtry = 3;
    while(!done && retry++ < maxtry)
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
    
    while(REDIS_OK == redisGetReply(_subscribe_context, (void **)&reply))
    {
        if(reply == nullptr) 
            continue;
        
        // 安全检查回复格式
        if(reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) 
        {
            
            // 检查消息类型
            string messageType = (reply->element[0] && reply->element[0]->str) ? 
                                reply->element[0]->str : "";
            
            if(messageType == "subscribe") 
            {
                // 订阅确认消息
                string channel = (reply->element[1] && reply->element[1]->str) ? 
                                reply->element[1]->str : "unknown";
                int count = (reply->element[2] && reply->element[2]->integer) ? 
                            reply->element[2]->integer : 0;

            }
            else if(messageType == "unsubscribe") 
            {
                // 取消订阅确认消息
                string channel = (reply->element[1] && reply->element[1]->str) ? 
                                reply->element[1]->str : "unknown";
                // cout << "[Redis] Unsubscribed from channel " << channel << endl;
            }
            else if(messageType == "message") 
            {
                // 实际消息
                if(reply->element[1] && reply->element[1]->str &&
                    reply->element[2] && reply->element[2]->str) {
                    
                    int channel = atoi(reply->element[1]->str);
                    string msg = reply->element[2]->str;

                    // 调用回调函数处理实际消息
                    if(_notify_message_handler) 
                        _notify_message_handler(channel, msg);
                }
            }
            else {
                cout << messageType << endl;
            }
        } else {
            cout << "无效消息" << reply->type << " elements=" << (reply->elements) << endl;
        }
        
        // 安全释放
        freeReplyObject(reply);
        reply = nullptr;
    }
    
    if(reply) {
        freeReplyObject(reply);
    }

}


void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}
