#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

using namespace std;


class ConnectionPool {
public:
    // 获取连接池单例实例
    static ConnectionPool* getInstance();
    
    // 获取数据库连接
    shared_ptr<MYSQL> getConnection();

    // 归还连接到连接池
    void returnConnection(shared_ptr<MYSQL> conn);
    
    // 查询
    MYSQL_RES* executeQuery(const string& sql);
    
    // 更新 
    bool executeUpdate(const string& sql);
    
    // 动态调整连接池大小
    void adjustPoolSize(int targetConnections);
    
    ~ConnectionPool();

private:
    ConnectionPool();
    
    // 创建连接
    MYSQL* createConnection();
    
    static ConnectionPool* instance;
    static mutex instanceMutex;
    
    queue<shared_ptr<MYSQL>> connectionQueue;
    mutex queueMutex;
    condition_variable condition;
    
    // 配置参数
    static const int MAX_CONNECTIONS = 10;
    static const int MIN_CONNECTIONS = 5;
    int currentConnections = 0;
    
    string server = "127.0.0.1";
    string user = "root";
    string password = "123";
    string dbname = "chat";
};

#endif
