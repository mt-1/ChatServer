#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;


// User表的增加方法
bool UserModel::insert(User &user)
{
    // 1 组装sql语句
    /*
    char sql[1024]={0}; 拼接 sql 之后，sql 占用的存储空间还是 1024 吗？还是拼接 sql 后的大小？
    sql 变量本身始终占用 1024 字节（数组大小不变）。
    拼接后的 SQL 语句内容只用到前面一部分空间（实际字符串长度+1），但整个数组依然占用 1024 字节内存。
    */
    char sql[1024] = {0};
    // char* 和 string的区别
    // char* 是C语言的字符串，string是C++的字符串类
    // C语言的字符串是字符数组，C++的字符串是类对象
    // sprintf函数的作用是将格式化的字符串输出到指定的字符数组中
    // 注意：sprintf函数的第一个参数是字符数组的指针，第二个参数是格式化字符串
    // sprintf函数的返回值是输出的字符数，不包括'\0'结尾符

    // 字符串常量在 SQL 里必须用单引号 '%s'，而不是双引号
    // char* 是 C 语言风格的字符串，本质是字符数组的指针，以 \0 结尾
    // user.getName() 返回的是 std::string，而 sprintf 需要 const char* 类型参数，所以要用 .c_str() 转换。
    // std::string 可以通过 .c_str() 方法获取底层的 C 风格字符串（const char*）。
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    MySQL mysql;
    if(mysql.connect())
    {
        if(mysql.update(sql))
        {
            // 获取插入成功的用户数据生成的主键id
            /*
            mysql_insert_id(mysql.getConnection())

            获取的是当前数据库连接上最近一次插入操作自动生成的自增 id。
            不会获取到别的 id，因为 MySQL 的自增 id 是和每个连接（Connection）绑定的。每个连接只会获取到自己刚刚插入的那条记录的 id，不会拿到其他连接插入的 id。
            mysql.getConnection() 作为参数很重要，它确保 mysql_insert_id 查询的是当前 MySQL 连接的插入记录。如果用错了连接对象，可能拿不到正确的 id。
            总结：只要你用的是同一个连接对象，mysql_insert_id(mysql.getConnection()) 就一定是你刚插入的那条数据的自增 id。
            */
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}


User UserModel::query(int id)
{

    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if(mysql.connect())
    {
            
        MYSQL_RES* res = mysql.query(sql);
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
        }
        
    }
    return User(); // 返回一个默认构造的User对象，表示查询失败
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1 组装sql语句
    char sql[1024] = {0};
    
    sprintf(sql, "update user set state = '%s' where id = '%d'", user.getState().c_str(), user.getId());

    MySQL mysql;
    if(mysql.connect())
    {
            
        if(mysql.update(sql))
        {
            return true; // 更新成功
        }
        
    }
    return false; // 返回一个默认构造的User对象，表示查询失败
}


// 重置状态的用户信息
void UserModel::resetState()
{
     // 1 组装sql语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    MySQL mysql;
    if(mysql.connect())
    {    
        mysql.update(sql);
    }

}
