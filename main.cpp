/*example for lib easyserver*/

#include<iostream>
#include"easy_server.h"
using namespace std;

INITIALIZE_EASYLOGGINGPP //easy logging

void TestTcpHandle(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len)
{
        std::cout<<EasyServer::ConvertBytes2HexString(data,len)<<endl;
}

int main(int argc, char *argv[])
{

        EasyServer es(10,20);
        if(!es.Init()){
                LOG(ERROR)<<"easyserver instance init failed!";
                return -1;
        }

        //for tcp protocol
        TcpPacketHandleCb tcphcb(TestTcpHandle,4003,false,false);
        es.AddTcpListener(tcphcb);
        es.Start();
        return 0;
}
