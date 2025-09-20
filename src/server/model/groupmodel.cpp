#include "groupmodel.hpp"
#include "connection_pool.h"
#include <iostream>

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into ALLGroup(groupname, groupdesc) values('%s', '%s')",
            group.getName().c_str(), group.getDesc().c_str());
    
    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    auto conn = pool->getConnection();
    
    if (conn && mysql_query(conn.get(), sql) == 0) {
        group.setId(mysql_insert_id(conn.get()));
        pool->returnConnection(conn);
        return true;
    }
    
    if (conn) {
        pool->returnConnection(conn);
    }
    return false;
}


// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into GroupUser values('%d', '%d', '%s')",
            groupid, userid, role.c_str());
    
    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    pool->executeUpdate(sql);
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{

    // 1 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select a.id, a.groupname, a.groupdesc from ALLGroup a inner join \
        GroupUser b on a.id = b.groupid where b.userid=%d", userid);
    
    vector<Group> groupVec;

    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    MYSQL_RES *res = pool->executeQuery(sql);
    
    if(res != nullptr)
    {
        MYSQL_ROW row;
        // 查出userid所有的群组信息
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            Group group;
            group.setId(atoi(row[0]));
            group.setName(row[1]);
            group.setDesc(row[2]);
            groupVec.push_back(group);
        }
        mysql_free_result(res);
    }
    
    // 查询群组的用户信息
    for(Group &group : groupVec)
    {
        sprintf(sql, "select a.id, a.name, a.state, b.grouprole from user a inner join \
            GroupUser b on b.userid = a.id where b.groupid=%d", group.getId());
        
        MYSQL_RES *res = pool->executeQuery(sql);
        if(res != nullptr)
        {
            MYSQL_ROW row;
            // 查处groupid群组的所有用户信息
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                group.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return groupVec;
}



// 根据指定的groupid查询群组用户id列表， 除userid自己， 主要用户群聊业务给群组其他成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from GroupUser where groupid=%d and userid != %d", groupid, userid);

    vector<int> idVec;
    
    // 使用连接池
    ConnectionPool* pool = ConnectionPool::getInstance();
    MYSQL_RES *res = pool->executeQuery(sql);
    
    if(res != nullptr)
    {
        MYSQL_ROW row;
        
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            idVec.push_back(atoi(row[0]));
        }
        mysql_free_result(res);
    }

    return idVec;
}

