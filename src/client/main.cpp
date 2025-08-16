#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"
#include "protocol.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

// 控制主菜单页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
atomic_bool g_isLoginSuccess{false};

// 上次收到服务器心跳响应的时间
std::atomic<time_t> g_lastPong{0}; 

// 心跳线程运行标志
std::atomic<bool> g_hbRun{true};

// 心跳间隔时间（秒）
const int HEARTBEAT_INTERVAL = 5;

// 心跳超时阈值（秒）
const int HEARTBEAT_TIMEOUT = 15;

// 接收线程
void readTaskHandler(int clientfd);

// 心跳线程
void heartbeatThread(int clientfd);

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

static void sendFrame(int fd, uint16_t msgid, const json& body)
{
    std::string frame = Protocol::encode(msgid, body);
    if(send(fd, frame.data(), frame.size(), 0) <= 0)
    {
        cerr << "send frame error" << endl;
        return;
    }
}

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach();                               // pthread_detach

    std::thread hb(heartbeatThread, clientfd);
    hb.detach(); // 心跳线程

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            // int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            

            sendFrame(clientfd, LOGIN_MSG, js);
            sem_wait(&rwsem); // 等待信号量，由子线程处理完登录的响应消息后，通知这里
                
            if (g_isLoginSuccess) 
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["name"] = name;
            js["password"] = pwd;
            // string request = js.dump();
            
            sendFrame(clientfd, REG_MSG, js);

            sem_wait(&rwsem); // 等待信号量，子线程处理完注册消息会通知
        }
        break;
        case 3: // quit业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

// 处理注册的响应逻辑
void doRegResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // 注册失败
    {
        cerr << "name is already exist, register error!" << endl;
    }
    else // 注册成功
    {
        cout << "name register success, userid is " << responsejs["id"]
                << ", do not forget it!" << endl;
    }
}

// 处理登录的响应逻辑
void doLoginResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // 登录失败
    {
        cerr << responsejs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 记录当前用户的id和name
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            // 初始化
            g_currentUserFriendList.clear();

            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if (responsejs.contains("groups"))
        {
            // 初始化
            g_currentUserGroupList.clear();

            vector<string> vec1 = responsejs["groups"];
            for (string &groupstr : vec1)
            {
                json grpjs = json::parse(groupstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string &userstr : vec2)
                {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }

                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息  个人聊天信息或者群组消息
        if (responsejs.contains("offlinemsg"))
        {
            vector<string> vec = responsejs["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + " said: " + xxx
                if (ONE_CHAT_MSG == js["msgid"].get<int>())
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }

        g_isLoginSuccess = true;
    }
}

// 子线程 - 接收线程
void readTaskHandler(int clientfd)
{
    std::string cache;
    cache.reserve(1024);
    char buf[1024];
    while(true)
    {
        int n = recv(clientfd, buf, sizeof(buf), 0);
        if(n <= 0)
        {
            close(clientfd);
            exit(-1);
        }
        cache.append(buf, n);
        while(true)
        {
            if(cache.size() < sizeof(MessageHeader)) break;
            MessageHeader hdr;
            memcpy(&hdr, cache.data(), sizeof(hdr));
            if(hdr.flag != Protocol::FLAG_NUMBER)
            {
                std::cerr << "protocol header invalid, close conn" << std::endl;
                close(clientfd);
                exit(-1);
            }

            // 看看是不是完整一帧长度
            size_t frameLen = sizeof(MessageHeader) + hdr.length;
            if(cache.size() < frameLen) break;


            MessageHeader realHdr;
            json js;
            if(!Protocol::decodeOne(cache.data(), frameLen, realHdr, js))
            {
                std::cerr << "decode frame failed, close conn, len: " << hdr.length << std::endl;
                close(clientfd);
                exit(-1);
            }
            cache.erase(0, frameLen);

            if(realHdr.msgid == HEARTBEAT_PONG)
            {
                // 更新最后一次收到服务器心跳响应的时间
                std::cout << "Received heartbeat pong from server." << std::endl;
                g_lastPong = time(nullptr);
                continue;
            }
            
            // 分发业务
            if (ONE_CHAT_MSG == realHdr.msgid)
            {
                cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                    << " said: " << js["msg"].get<string>() << endl;
                continue;
            }

            if (GROUP_CHAT_MSG == realHdr.msgid)
            {
                cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                    << " said: " << js["msg"].get<string>() << endl;
                continue;
            }

            if (LOGIN_MSG_ACK == realHdr.msgid)
            {
                doLoginResponse(js); // 处理登录响应的业务逻辑
                sem_post(&rwsem);    // 通知主线程，登录结果处理完成
                continue;
            }

            if (REG_MSG_ACK == realHdr.msgid)
            {
                doRegResponse(js);
                sem_post(&rwsem);    // 通知主线程，注册结果处理完成
                continue;
            }
        }
    }

    // for (;;)
    // {
    //     char buffer[1024] = {0};
    //     int len = recv(clientfd, buffer, 1024, 0);  // 阻塞了
    //     if (-1 == len || 0 == len)
    //     {
    //         close(clientfd);
    //         exit(-1);
    //     }

    //     cout << "Received data from server: [" << buffer << "]" << endl;

    //     // 接收ChatServer转发的数据，反序列化生成json数据对象
    //     json js = json::parse(buffer);
    //     int msgtype = js["msgid"].get<int>();
    //     if (ONE_CHAT_MSG == msgtype)
    //     {
    //         cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
    //              << " said: " << js["msg"].get<string>() << endl;
    //         continue;
    //     }

    //     if (GROUP_CHAT_MSG == msgtype)
    //     {
    //         cout << "群消息[" << js["groupid"] << "]:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
    //              << " said: " << js["msg"].get<string>() << endl;
    //         continue;
    //     }

    //     if (LOGIN_MSG_ACK == msgtype)
    //     {
    //         doLoginResponse(js); // 处理登录响应的业务逻辑
    //         sem_post(&rwsem);    // 通知主线程，登录结果处理完成
    //         continue;
    //     }

    //     if (REG_MSG_ACK == msgtype)
    //     {
    //         doRegResponse(js);
    //         sem_post(&rwsem);    // 通知主线程，注册结果处理完成
    //         continue;
    //     }
    // }
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令,格式help"},
    {"chat", "一对一聊天,格式chat:friendid:message"},
    {"addfriend", "添加好友,格式addfriend:friendid"},
    {"creategroup", "创建群组,格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组,格式addgroup:groupid"},
    {"groupchat", "群聊,格式groupchat:groupid:message"},
    {"loginout", "注销,格式loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

// 主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command; // 存储命令
        int idx = commandbuf.find(":");
        if (-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}
// "addfriend" command handler
void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;

    sendFrame(clientfd, ADD_FRIEND_MSG, js);

    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    // if (-1 == len)
    // {
    //     cerr << "send addfriend msg error -> " << buffer << endl;
    // }
}
// "chat" command handler
void chat(int clientfd, string str)
{
    int idx = str.find(":"); // friendid:message
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1);

    json js;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    // std::string f1 = Protocol::encode(ONE_CHAT_MSG, js);
    // json js2 = js;
    // js2["msg"] = std::string("SECOND_")+message;
    // std::string f2 = Protocol::encode(ONE_CHAT_MSG, js2);
    // std::string combo = f1 + f2;

    // send(clientfd, combo.data(), combo.size(), 0);
    sendFrame(clientfd, ONE_CHAT_MSG, js);



    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);

    // if (-1 == len)
    // {
    //     cerr << "send chat msg error -> " << combo << endl;
    // }
    // else
    // {
    //     cout << "send chat msg success -> " << combo << endl;
    // }
}
// "creategroup" command handler  groupname:groupdesc
void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;

    sendFrame(clientfd, CREATE_GROUP_MSG, js);

    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    // if (-1 == len)
    // {
    //     cerr << "send creategroup msg error -> " << buffer << endl;
    // }
}
// "addgroup" command handler
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;

    sendFrame(clientfd, ADD_GROUP_MSG, js);

    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    // if (-1 == len)
    // {
    //     cerr << "send addgroup msg error -> " << buffer << endl;
    // }
}
// "groupchat" command handler   groupid:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    sendFrame(clientfd, GROUP_CHAT_MSG, js);

    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    // if (-1 == len)
    // {
    //     cerr << "send groupchat msg error -> " << buffer << endl;
    // }
}
// "loginout" command handler
void loginout(int clientfd, string)
{
    json js;
    js["id"] = g_currentUser.getId();

    sendFrame(clientfd, LOGINOUT_MSG, js);
    isMainMenuRunning = false; // 退出主菜单页面
    g_hbRun = false; // 停止心跳线程

    
    // string buffer = js.dump();
    // int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    // if (-1 == len)
    // {
    //     cerr << "send loginout msg error -> " << buffer << endl;
    // }
    // else
    // {
    //     isMainMenuRunning = false;
    // }   
}

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);

    return std::string(date);
}

void heartbeatThread(int fd)
{
    g_lastPong = time(nullptr);
    // int pingCount = 0;
    while(g_hbRun)
    {
        // pingCount++;
        // if(pingCount <= 3)
        // {
        //     std::cout << "heartbeat thread running..." << std::endl;
        //     json ping;
        //     ping["ts"] = time(nullptr); // 发送心跳包时的时间戳
        //     sendFrame(fd, HEARTBEAT_PING, ping);
        // }
        // else
        //     std::cout << " simulate heartbeat stop..." << std::endl;


        std::cout << "heartbeat thread running..." << std::endl;
        json ping;
        ping["ts"] = time(nullptr); // 发送心跳包时的时间戳
        sendFrame(fd, HEARTBEAT_PING, ping);


        for (int i = 0; i<HEARTBEAT_INTERVAL && g_hbRun; ++i)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (time(nullptr) - g_lastPong.load() > HEARTBEAT_TIMEOUT)
            {
                cerr << "heartbeat timeout, exit..." << endl;
                close(fd);
                exit(-1);
            }
        }

        // 每隔HEARTBEAT_INTERVAL秒发送一次心跳包
        // std::this_thread::sleep_for(chrono::seconds(HEARTBEAT_INTERVAL));

        // for(int i = 0; i < HEARTBEAT_INTERVAL; ++i)
        // {
        //     std::this_thread::sleep_for(std::chrono::seconds(1));
        //     if(!g_hbRun) return; // 如果心跳线程被停止，则退出
        // }
    }
}