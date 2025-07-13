#include "json.hpp"
using json = nlohmann::json;

#include <iostream>
#include <vector>
#include <map>

using namespace std;

string func1()
{
    json js;
    js["msg_type"] = 2;
    js["from"] = "zhang san";
    js["to"] = "li si";
    js["msg"] = " hello world";

    string sendBuf = js.dump();
    // cout << sendBuf.c_str() << endl;
    return sendBuf;
}

string func2()
{
    json js;
    js["id"] = {1,2,3,4,5};
    js["name"] = "zhang san";
    
    js["msg"]["zhang san"] = "hello world";
    js["msg"]["li si"] = "aaa";
    // 上面等同于下面这句一次性添加数组对象
    js["msg"] = {{"zhang san", "hello world"}, {"li si", "aaa"}};
    // cout << js << endl;
    return js.dump(); // json数据对象---》序列化 json字符串
}


string func3()
{
    json js;

    vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    js["list"] = vec;
    map<int, string> m;
    m.insert({1,"黄山"});
    m.insert({2,"华山"});
    m.insert({3,"泰山"});

    js["path"] = m;

    string sendBuf = js.dump();  // json数据对象---》序列化 json字符串
    // cout << sendBuf << endl;
    return sendBuf;
}

int main()
{

    // string recvBuf = func1();
    // // 数据的反序列化   json字符串--》反序列化成数据对象（看作容器，方便访问）
    // json jsbuf = json::parse(recvBuf);
    // cout << jsbuf["msg_type"] << endl;
    // cout << jsbuf["from"] << endl;
    // cout << jsbuf["to"] << endl;
    // cout << jsbuf["msg"] << endl;


    // string recvBuf = func2();
    // json jsbuf = json::parse(recvBuf);

    // cout << jsbuf["id"] << endl;
    // auto arr = jsbuf["id"];
    // cout << arr[2] << endl;

    // auto msgjs = jsbuf["msg"];
    // cout << msgjs["zhang san"] << endl;
    // cout << msgjs["li si"] << endl;


    string recvBuf = func3();
    json jsbuf = json::parse(recvBuf);

    vector<int> vec = jsbuf["list"];  // js对象里面的数组类型直接放入vector容器中
    for(int &v : vec)
    {
        cout << v << " ";
    }
    cout << endl;

    map<int, string> m = jsbuf["path"];  // js对象里面的map类型直接放入map容器中
    for(auto &p : m)
    {
        cout << p.first << " " << p.second << endl;
    }
    cout << endl;

    return 0;
}



