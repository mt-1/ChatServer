#include "connection_pool.h"
#include <iostream>
#include <chrono>


ConnectionPool* ConnectionPool::instance = nullptr;
mutex ConnectionPool::instanceMutex;


// 锁+双重判断，避免每次调用都加锁的性能开销
ConnectionPool* ConnectionPool::getInstance() {
    if (instance == nullptr) {
        lock_guard<mutex> lock(instanceMutex);
        // 锁+双重判断
        if (instance == nullptr) {
            instance = new ConnectionPool();
        }
    }
    return instance;
}

// 创建连接池
ConnectionPool::ConnectionPool() {
    for (int i = 0; i < MIN_CONNECTIONS; ++i) {
        MYSQL* conn = createConnection();
        if (conn) {
            // 智能指针管理连接，自定义删除器确保连接正确释放
            connectionQueue.push(shared_ptr<MYSQL>(conn, [](MYSQL* c) { 
                mysql_close(c); 
            }));
            currentConnections++;
        }
    }
    std::cout << "连接池初始化完成，连接数: " << currentConnections << std::endl;
}

// 创建新的数据库连接
MYSQL* ConnectionPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        std::cout << "mysql_init 失败" << std::endl;
        return nullptr;
    }
    
    MYSQL* result = mysql_real_connect(conn, server.c_str(), user.c_str(), 
                                      password.c_str(), dbname.c_str(), 
                                      3306, nullptr, 0);
    if (!result) {
        std::cout << "mysql连接失败: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return nullptr;
    }
    
    // 设置字符集
    mysql_query(conn, "set names utf8mb4");
    
    return conn;
}

// 从连接池获取连接
shared_ptr<MYSQL> ConnectionPool::getConnection() {
    unique_lock<mutex> lock(queueMutex);
    
    // 如果没有可用连接且未达到最大连接数，创建新连接
    if (connectionQueue.empty() && currentConnections < MAX_CONNECTIONS) {
        lock.unlock(); // 释放锁来创建连接
        MYSQL* newConn = createConnection();
        lock.lock(); // 重新获取锁
        
        if (newConn) {
            currentConnections++;
            return shared_ptr<MYSQL>(newConn, [this](MYSQL* c) { 
                if (c) {
                    mysql_close(c);
                    // 减少连接计数
                    lock_guard<mutex> countLock(queueMutex);
                    currentConnections--;
                }
            });
        }
    }
    
    // 等待连接可用，但不要等太久
    if (connectionQueue.empty()) {
        condition.wait_for(lock, chrono::seconds(3), [this] { return !connectionQueue.empty(); });
    }
    
    if (connectionQueue.empty()) {
        std::cout << "连接池中没有可用连接，当前连接数: " << currentConnections << std::endl;
        return nullptr;
    }
    
    shared_ptr<MYSQL> conn = connectionQueue.front();
    connectionQueue.pop();
    
    // 在持有锁的情况下，快速检查连接
    if (!conn) {
        std::cout << "从队列中取出空连接" << std::endl;
        return nullptr;
    }
    
    // 释放锁后再进行耗时的连接检查
    lock.unlock();
    
    // 检查连接是否有效 - 使用更安全的方式
    try {
        if (mysql_ping(conn.get()) != 0) {
            std::cout << "连接已失效，重新创建连接" << std::endl;
            
            // 创建新连接替换失效的连接
            MYSQL* newConn = createConnection();
            if (newConn) {
                // 创建新的智能指针替换旧的
                return shared_ptr<MYSQL>(newConn, [this](MYSQL* c) { 
                    if (c) {
                        mysql_close(c);
                        lock_guard<mutex> countLock(queueMutex);
                        currentConnections--;
                    }
                });
            } else {
                std::cout << "创建新连接失败" << std::endl;
                // 旧连接会在智能指针析构时自动关闭
                return nullptr;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "检查连接有效性时出现异常: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cout << "检查连接有效性时出现未知异常" << std::endl;
        return nullptr;
    }
    
    return conn;
}

// 归还连接到连接池
void ConnectionPool::returnConnection(shared_ptr<MYSQL> conn) {
    if (!conn) {
        return;
    }
    
    lock_guard<mutex> lock(queueMutex);
    // 简单地将连接归还到连接池，不做有效性检查
    // 有效性检查在getConnection时进行
    connectionQueue.push(conn);
    condition.notify_one();
}



// 执行查询操作
MYSQL_RES* ConnectionPool::executeQuery(const string& sql) {
    auto conn = getConnection();
    if (!conn) {
        std::cout << "获取连接失败，查询: " << sql << std::endl;
        return nullptr;
    }
    
    MYSQL_RES* result = nullptr;
    
    try {
        if (mysql_query(conn.get(), sql.c_str())) {
            std::cout << "查询失败: " << sql << " 错误: " << mysql_error(conn.get()) << std::endl;
            returnConnection(conn);
            return nullptr;
        }
        
        // 使用 mysql_store_result 一次性获取所有结果
        result = mysql_store_result(conn.get());
        
        // 检查是否有错误发生
        if (result == nullptr && mysql_field_count(conn.get()) > 0) {
            std::cout << "获取查询结果失败: " << sql << " 错误: " << mysql_error(conn.get()) << std::endl;
            returnConnection(conn);
            return nullptr;
        }
        
        returnConnection(conn);
        return result;
        
    } catch (const std::exception& e) {
        std::cout << "执行查询时发生异常: " << e.what() << " SQL: " << sql << std::endl;
        if (result) {
            mysql_free_result(result);
        }
        returnConnection(conn);
        return nullptr;
    } catch (...) {
        std::cout << "执行查询时发生未知异常，SQL: " << sql << std::endl;
        if (result) {
            mysql_free_result(result);
        }
        returnConnection(conn);
        return nullptr;
    }
}

// 执行更新操作
bool ConnectionPool::executeUpdate(const string& sql) {
    auto conn = getConnection();
    if (!conn) {
        std::cout << "获取连接失败，更新: " << sql << std::endl;
        return false;
    }
    
    bool success = false;
    
    try {
        success = (mysql_query(conn.get(), sql.c_str()) == 0);
        if (!success) {
            std::cout << "更新失败: " << sql << " 错误: " << mysql_error(conn.get()) << std::endl;
        }
        
        returnConnection(conn);
        return success;
        
    } catch (const std::exception& e) {
        std::cout << "执行更新时发生异常: " << e.what() << " SQL: " << sql << std::endl;
        returnConnection(conn);
        return false;
    } catch (...) {
        std::cout << "执行更新时发生未知异常，SQL: " << sql << std::endl;
        returnConnection(conn);
        return false;
    }
}

// 动态调整连接池大小
void ConnectionPool::adjustPoolSize(int targetConnections) {
    if (targetConnections < MIN_CONNECTIONS) {
        targetConnections = MIN_CONNECTIONS;
    }
    if (targetConnections > MAX_CONNECTIONS) {
        targetConnections = MAX_CONNECTIONS;
    }
    
    lock_guard<mutex> lock(queueMutex);
    
    if (targetConnections > currentConnections) {
        // 增加连接
        int toAdd = targetConnections - currentConnections;
        for (int i = 0; i < toAdd; ++i) {
            MYSQL* conn = createConnection();
            if (conn) {
                connectionQueue.push(shared_ptr<MYSQL>(conn, [](MYSQL* c) { mysql_close(c); }));
                currentConnections++;
            } else {
                break;
            }
        }
        std::cout << "连接池大小增加到 " << currentConnections << " 个连接" << std::endl;
    } else if (targetConnections < currentConnections) {
        // 减少连接
        int toRemove = currentConnections - targetConnections;
        while (toRemove > 0 && !connectionQueue.empty()) {
            connectionQueue.pop();
            currentConnections--;
            toRemove--;
        }
        std::cout << "连接池大小减少到 " << currentConnections << " 个连接" << std::endl;
    }
}

ConnectionPool::~ConnectionPool() {
    lock_guard<mutex> lock(queueMutex);
    while (!connectionQueue.empty()) {
        connectionQueue.pop();
    }
    std::cout << "连接池已销毁" << std::endl;
}
