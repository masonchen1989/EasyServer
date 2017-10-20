#ifndef COMMON_STRUCTS_H_
#define COMMON_STRUCTS_H_
#define ELPP_THREAD_SAFE
#include"easylogging++.h"
#include"nedmalloc.h"
#include<string>
#include<event2/listener.h>
#include<event2/bufferevent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include<event2/buffer.h>

class WorkerThread;
struct SocketPort{
        int sock;
        int port;
};


struct SessionData
{
    SessionData(void* d,int l,const std::string& sid,void * a=NULL,int al=0){
        data=d;
        len=l;
        sessionid=sid;
        arg=a;
        arglen=al;
        /* cbtype=CRESULTCB; */
    }

    SessionData(const std::string& sid,const std::string& sdata,const std::string& sarg){
        strdata=sdata;
        sessionid=sid;
        strarg=sarg;
        data=NULL;
        len=0;
        arg=NULL;
        arglen=0;
        /* cbtype=ct; */
    }
    void * data;
    int len;
    void * arg;
    int arglen;
    std::string strarg;
    std::string strdata;
    std::string sessionid;
    bool hasResultCb;
};

struct SessionKill{
SessionKill(const std::string& sid)
:sessionid(sid)
        {}
    std::string sessionid;
};

struct TcpConnItem{
        TcpConnItem(int s,int p,int i,const std::string& sid)
                {
                        sock=s;
                        port=p;
                        /* thread=t; */
                        threadindex=i;
                        buff=NULL;
                        data=NULL;
                        totallength=0;
                        remaininglength=0;
                        sessionid=sid;
                        /* isfirstpacket=true; */
                        handlefunindex=-1;
                }

        virtual ~TcpConnItem()
                {
                    FreeData();
                    if(buff)
                        bufferevent_free(buff);
                }

        bool AllocateCopyData( struct evbuffer * in,size_t * buffer_len,unsigned short length=0)//allocate need length
        {
            bool ret=false;
            do{
                if(!totallength){
                    data=nedalloc::nedmalloc(length);
                    if(!data)
                        break;
                    totallength=length;
                    remaininglength=length;
                }

                unsigned short copied=evbuffer_remove(in,data+totallength-remaininglength,remaininglength);
                if(copied==-1)
                    break;
                remaininglength-=copied;
                *buffer_len-=copied;
                ret=true;

            }while (0);
            return ret;
        }

    void FreeData()
        {
            if(data){
                LOG(DEBUG)<<"Freed "<<totallength<<" bytes data from session "<<sessionid;
                nedalloc::nedfree(data);
                data=NULL;
            }
            totallength=0;
            remaininglength=0;
        }

    void ReleaseDataOwnership()
        {
            data=NULL;
            totallength=0;
            remaininglength=0;
        }
        int sock;
        int port;
        /* WorkerThread * thread; */
        int threadindex;
        bufferevent * buff;
        void * data;
        unsigned int totallength;
        unsigned int remaininglength;
        int handlefunindex;
        /* bool isfirstpacket; */
        std::string sessionid;
};
#endif
