#include "usermodel.hpp"
#include "db.h"
#include "connection_pool.h" 
#include <iostream>
using namespace std;


// User表的增加方法
bool UserModel::insert(User &user)
{

    char sql[1024] = {0};

    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    // 使用连接池执行插入操作
    ConnectionPool* pool = ConnectionPool::getInstance();
    auto conn = pool->getConnection();
    
    if (conn && mysql_query(conn.get(), sql) == 0) {
        user.setId(mysql_insert_id(conn.get()));
        pool->returnConnection(conn);
        return true;
    }
    
    if (conn) {
        pool->returnConnection(conn);
    }
    return false;
}


User UserModel::query(int id)
{
    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    MYSQL_RES* res = pool->executeQuery(sql);
    
    if(res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        if(row != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setPwd(row[2]);
            user.setState(row[3]);

            mysql_free_result(res);
            return user;
        }
        mysql_free_result(res);
    }
    
    return User(); // 返回一个默认构造的User对象，表示查询失败
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    return pool->executeUpdate(sql);
}


// 重置状态的用户信息
void UserModel::resetState()
{
     // 1 组装sql语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    pool->executeUpdate(sql);
}
