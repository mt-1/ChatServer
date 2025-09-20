#include "chatservice.hpp"
#include "public.hpp"
#include "protocol.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include <iostream>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <db.h>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
/*
为什么要设计成单例模式？单例模式有什么好处？讲解一下单例模式是什么
单例模式（Singleton）是一种设计模式，保证一个类只有一个实例，并提供全局访问点。
在服务端，像 ChatService 这种核心服务类，通常只需要一个实例来管理所有业务和资源，避免多实例带来的资源冲突和数据不一致。
好处：
节省资源，避免重复创建对象。
保证全局唯一性，方便管理和维护。
提供统一的访问接口。
实现方式：通常用静态成员变量+私有构造函数+静态方法获取实例（如 ChatService::instance()）。
*/
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});


    // _msgHandlerMap.insert({HEARTBEAT_PING, std::bind(&ChatService::heartbeat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 获取消息对应的处理器
/*
getHandler 根据消息类型（msgid）返回对应的消息处理函数（Handler）。
这样主线程收到消息后，可以通过 msgid 查找并调用对应的业务处理逻辑，实现消息分发和解耦。
如果找不到对应的 Handler，会返回一个空操作并记录错误日志，防止程序崩溃。
*/
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 处理登录业务  id pwd
/*
为什么这里要注意线程安全？
因为 _userConnMap 这个用户连接表可能会被多个线程同时访问（如多个客户端并发登录、退出、收消息等），
如果不加锁，可能会出现数据竞争、崩溃或数据错误。
lock_guard<mutex> 保证同一时刻只有一个线程能修改 _userConnMap，确保数据安全。

conn->send(response.dump()); 为什么都要 .dump()？客户端发给服务器是发的什么？服务端返回给客户端是什么？
.dump() 是把 json 对象序列化成字符串（JSON 格式文本）。
网络通信只能发送字符串或二进制数据，不能直接发 json 对象。
客户端发给服务器、服务器返回给客户端的都是JSON 格式的字符串，如：
{"msgid":1,"id":2,"password":"123456"}
这样双方都能方便地解析和处理消息内容。
*/
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            json response;
            response["errno"] = 2; // 0表示成功
            response["errmsg"] = "该帐号已经登录，请不要重复登录";
            conn->send(Protocol::encode(LOGIN_MSG_ACK, response));
            return ;
        }

        // 登录成功，记录用户连接信息
        {
            lock_guard<mutex> lock(_connMutex);
            _userConnMap.insert({id, conn});
        }

        // id用户登录成功后，向redis订阅channel(id)
        if(!_redis.subscribe(id)) {
            cerr << "[Login] Failed to subscribe to channel " << id << endl;
        } 

        // 登录成功，更新用户状态信息  state  offline=》online
        user.setState("online");
        _userModel.updateState(user);

        json response;
        response["errno"] = 0; // 0表示成功
        response["msgid"] = LOGIN_MSG_ACK;
        response["id"] = user.getId();
        response["name"] = user.getName();
        // 查看该用户是否有离线消息
        vector<string> vec = _offlineMsgModel.query(id);
        if(!vec.empty())
        {
            response["offlinemsg"] = vec;
            // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
            _offlineMsgModel.remove(id);
        }
        
        // 查询该用户的好友信息并返回
        vector<User> userVec = _friendModel.query(id);
        if(!userVec.empty())
        {
            vector<string> vec2;
            for(User &user : userVec)
            {
                json ujs;
                ujs["id"] = user.getId();
                ujs["name"] = user.getName();
                ujs["state"] = user.getState();
                vec2.push_back(ujs.dump());
            }
            response["friends"] = vec2;
        }

        // 查询该用户的群组信息并返回
        vector<Group> groupVec = _groupModel.queryGroups(id);
        if(!groupVec.empty())
        {
            vector<string> vec3;
            for(Group &group : groupVec)
            {
                json gjs;
                gjs["id"] = group.getId();
                gjs["groupname"] = group.getName();
                gjs["groupdesc"] = group.getDesc();
                vector<string> userV;
                for(GroupUser &user : group.getUsers())
                {
                    json ujs;
                    ujs["id"] = user.getId();
                    ujs["name"] = user.getName();
                    ujs["state"] = user.getState();

                    ujs["role"] = user.getRole();
                    userV.push_back(ujs.dump());
                }
                gjs["users"] = userV;
                vec3.push_back(gjs.dump());
            }
            response["groups"] = vec3;
        }
        // conn->send(response.dump());
        conn->send(Protocol::encode(LOGIN_MSG_ACK, response));

    }
    else
    {
        // 该用户不存在，登录失败 / 密码错误
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误";
        // conn->send(response.dump());
        conn->send(Protocol::encode(LOGIN_MSG_ACK, response));
    }
}
// 处理注册业务 name password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];   
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0; // 0表示成功
        response["id"] = user.getId();
        // conn->send(response.dump());
        conn->send(Protocol::encode(REG_MSG_ACK, response));
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1; // 1表示失败
        // conn->send(response.dump());
        conn->send(Protocol::encode(REG_MSG_ACK, response));
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it != _userConnMap.end())
        {
            // 从map表删除用户的连接信息
            _userConnMap.erase(it);
        }
    }

    // 向redis取消订阅channel(userid)
    _redis.unsubscribe(userid);

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    if (!conn) {
        LOG_WARN << "Null connection in clientCloseException";
        return;
    }

    User user;
    bool found = false;

    // 从连接映射中查找并移除用户
    {
        lock_guard<mutex> lock(_connMutex);
        for(auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if(it->second == conn)
            {
                user.setId(it->first);
                _userConnMap.erase(it);
                found = true;
                LOG_INFO << "Removed user " << user.getId() << " from connection map";
                break;
            }
        }
    }

    // 如果找到了用户，进行清理
    if(found && user.getId() > 0)
    {
        int userId = user.getId();
        LOG_INFO << "[Cleanup] Processing cleanup for user " << userId;
        
        try {
            // 同步处理，避免程序退出时异步线程访问已销毁的对象
            // Redis取消订阅
            if(!_redis.unsubscribe(userId)) {
                LOG_ERROR << "[Cleanup] Failed to unsubscribe user " << userId;
            }
            
            // 更新数据库状态
            User offlineUser;
            offlineUser.setId(userId);
            offlineUser.setState("offline");
            if(_userModel.updateState(offlineUser)) {
                LOG_INFO << "[Cleanup] Updated user " << userId << " state to offline";
            } else {
                LOG_ERROR << "[Cleanup] Failed to update user " << userId << " state";
            }
            
            LOG_INFO << "[Cleanup] Cleanup completed for user " << userId;
            
        } catch(const std::exception& e) {
            LOG_ERROR << "[Cleanup] Exception during cleanup: " << e.what();
        } catch(...) {
            LOG_ERROR << "[Cleanup] Unknown exception during cleanup";
        }
    }
}


// 生成唯一ID
string ChatService::generateUUID() {
    static std::atomic<uint64_t> counter{0};
    return std::to_string(_serverPort) + "_" + 
           std::to_string(time(nullptr)) + "_" + 
           std::to_string(counter++);
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>();
    int fromid = js["id"].get<int>();
    js["server_id"] = _serverPort;      // 服务器标识
    js["msg_uuid"] = generateUUID();    // 全局唯一消息ID，用于去重和追踪

    // 检查目标用户是否在本地在线
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if(it != _userConnMap.end())
        {
            it->second->send(Protocol::encode(ONE_CHAT_MSG, js));
            return;
        }
    }

    cout << "[OneChat] User " << toid << " not found locally" << endl;

    // 查询数据库获取用户状态
    User user = _userModel.query(toid);
    string userState = (user.getId() == toid) ? user.getState() : "offline";
    cout << "[OneChat] User " << toid << " database state: '" << userState << "'" << endl;
    
    if(userState == "online") {   
        bool publishResult = _redis.publish(toid, js.dump());

        if(!publishResult) {
            _offlineMsgModel.insert(toid, js.dump());
        }
        return;
    }

    // 用户离线，存储离线消息

    _offlineMsgModel.insert(toid, js.dump());
}

// 添加好友业务  msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if(_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for(int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if(it != _userConnMap.end())
        {
            // 转发群消息
            // it->second->send(js.dump());
            it->second->send(Protocol::encode(GROUP_CHAT_MSG, js));
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if(user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string message)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end())
    {
        // 用户在线，直接推送消息
        // it->second->send(message);

        json js = json::parse(message);
        if(js.contains("server_id"))
        {
            int serverId = js["server_id"].get<int>();
            if(serverId == _serverPort)
            {
                return ;
            }
        }
        
        uint16_t msgid = js.contains("msgid") ? js["msgid"].get<int>() : ONE_CHAT_MSG;
        it->second->send(Protocol::encode(msgid, js));
        return ;
    }

    // 用户不在线，存储离线消息
    _offlineMsgModel.insert(userid, message);
    
}

// 心跳处理
void ChatService::heartbeat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 更新连接最后时间
    // conn->setContext(time.secondsSinceEpoch());
    std::cout << "接收心跳: " << conn->name() << std::endl;

    // std::cout << "time.now(): " << time.now().toString() 
    // << "time.secondSinceEpoch" <<  time.secondsSinceEpoch() << std::endl;

    // 回复心跳包
    json res;
    res["time"] = time.secondsSinceEpoch();
    conn->send(Protocol::encode(HEARTBEAT_PONG, res));

    std::cout << "发送心跳: " << conn->name() << std::endl;
}


// {"msgid":3,"name":"li si","password":"123456"}
// {"msgid":1,"id":2,"password":"123456"}
// {"msgid":1,"id":1,"password":"123"}
// {"msgid":5,"id":1, "from":"zhang san", "to":2, "msg":"hello222"}
// {"msgid":5,"id":2, "from":"li si", "to":1, "msg":"ok"}


// {"msgid":6,"id":1,"friendid":2}