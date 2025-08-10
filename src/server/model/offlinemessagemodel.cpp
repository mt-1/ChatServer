#include "offlinemessagemodel.hpp"
#include "db.h"



// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into offlineMessage(userid, message) values(%d, '%s')", userid, msg.c_str());

    MySQL mysql;
    if(mysql.connect())
    {
        mysql.update(sql);
    }

}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlineMessage where userid=%d", userid);

    MySQL mysql;
    if(mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    // 1. 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from offlineMessage where userid=%d", userid);

    MySQL mysql;
    vector<string> vec;
    if(mysql.connect())
    {
        MYSQL_RES* res = mysql.query(sql);
        if(res != nullptr)
        {
            MYSQL_ROW row;
            // 把userid用户的所有离线消息放入vec中返回
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}