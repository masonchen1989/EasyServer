/*
 * easy_server.cpp
 *
 *  Created on: 2016年9月28日
 *      Author: chenms
 */
#include "easy_server.h"
#include "common_structs.h"
#include "worker_thread.h"

thread_local int thread_local_port=-1;
thread_local bool isworkerthread=false;



EasyServer::EasyServer(int num_of_workers, int num_of_threads_in_threadpool)
{
    num_of_workers_=num_of_workers;
    num_of_threads_in_threadpool_=num_of_threads_in_threadpool;
    last_thread_index_=-1;
    getresultcb_=NULL;
    tcpclosecb_=NULL;
}

EasyServer::~EasyServer()
{
    for(auto pos=vec_tcp_listeners_.begin();pos!=vec_tcp_listeners_.end();++pos)
    {
        if(pos->tcp_listen_base)
            event_base_free(pos->tcp_listen_base);
        if(pos->tcp_listener)
            evconnlistener_free(pos->tcp_listener);
    }

    for(auto pos=vec_udp_listeners_.begin();pos!=vec_udp_listeners_.end();++pos)
    {
        if(pos->udp_listen_base)
            event_base_free(pos->udp_listen_base);
        if(pos->udp_listen_event)
            event_free(pos->udp_listen_event);
        if(pos->udp_listen_socket!=-1)
            close(pos->udp_listen_socket);
    }

    for(auto pos=vec_overtime_listeners_.begin();pos!=vec_overtime_listeners_.end();++pos)
    {
        if(pos->overtime_listen_base)
            event_base_free(pos->overtime_listen_base);
        if(pos->overtime_listen_event)
            event_free(pos->overtime_listen_event);
    }

}

// bool EasyServer::AddTcpListener(int port)
// {
//     TcpListener tl;
//     if(!StartTcpListen(tl,port)){
//         return false;
//     }

//     vec_tcp_listeners_.push_back(tl);
//     return true;
// }

bool EasyServer::AddTcpListener(int port,TcpPacketHandleCb& tphcb)
{
    TcpListener tl;
    if(!StartTcpListen(tl,port)){
        return false;
    }

    vec_tcp_listeners_.push_back(tl);
    vec_tcppackethandlecbs_.push_back(tphcb);
    return true;
}

// bool EasyServer::AddUdpListener(int port)
// {
//     UdpListener ul;
//     if(!StartUdpListen(ul,port))
//     {
//         return false;
//     }

//     vec_udp_listeners_.push_back(ul);
//     return true;
// }

bool EasyServer::AddUdpListener(int port,UdpPacketHandleCb& udpcb)
{
    UdpListener ul;
    if(!StartUdpListen(ul,port))
    {
        return false;
    }

    vec_udp_listeners_.push_back(ul);
    vec_udppackethandlecbs_.push_back(udpcb);
    return true;
}

bool EasyServer::AddOvertimeListener(int tm,overtime_cb cb,void * arg)
{
    OvertimeListener ol;
    if(!StartOvertimeListen(ol,tm,cb,arg))
    {
        return false;
    }

    vec_overtime_listeners_.push_back(ol);
    return true;
}

bool EasyServer::StartTcpListen(TcpListener& tl,int port)
{
    event_base * base=NULL;
    evconnlistener *listener=NULL;
    std::shared_ptr<std::thread> listen_thread;
    do{
        struct sockaddr_in sin;
        base = event_base_new();
        if (!base)
            break;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0);
        sin.sin_port = htons(port);

        listener = evconnlistener_new_bind(base,EasyServer::AcceptTcpConn, this,LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1, (struct sockaddr*)&sin, sizeof(sin));
        if (!listener)
            break;
        evconnlistener_set_error_cb(listener,EasyServer::AcceptTcpError);

        try{
            listen_thread.reset(new std::thread([this,port,base]
                                                {
                                                    thread_local_port=port;
                                                    event_base_dispatch(base);
                                                }
                                    ));
        }catch(...){
            break;
        }

        tl.tcp_listen_base=base;
        tl.tcp_listener=listener;
        tl.tcp_listen_thread=listen_thread;

        return true;
    }while(0);

    if(base)
        event_base_free(base);
    if(listener)
        evconnlistener_free(listener);

    LOG(ERROR)<<"Tcp listen on the port "<<port<<" failed";
    return false;
}

bool EasyServer::StartUdpListen(UdpListener& ul,int port)
{
    event_base * base=NULL;
    struct event  * listen_event=NULL;
    int listen_socket=-1;
    std::shared_ptr<std::thread> listen_thread;

    do{
        listen_socket=socket(AF_INET,SOCK_DGRAM,0);
        if(listen_socket==-1)
        {
            break;
        }

        struct sockaddr_in sin;
        base = event_base_new();
        if (!base)
            break;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0);
        sin.sin_port = htons(port);
        if(bind(listen_socket,(const sockaddr *)&sin,sizeof(sin))==-1)
        {
            break;
        }

        listen_event=event_new(base,listen_socket,EV_READ | EV_PERSIST,EasyServer::AcceptUdpConn,(void *)this);
        if(!listen_event)
            break;
        if(event_add(listen_event, 0))
            break;

        try{
            listen_thread.reset(new std::thread([this,port,base]
                                                {
                                                    thread_local_port=port;
                                                    event_base_dispatch(base);
                                                }
                                    ));
        }catch(...){
            break;
        }

        ul.udp_listen_base=base;
        ul.udp_listen_event=listen_event;
        ul.udp_listen_socket=listen_socket;
        ul.udp_listen_thread=listen_thread;

        return true;
    }while(0);

    if(base)
        event_base_free(base);
    if(listen_event)
        event_free(listen_event);
    if(listen_socket!=-1)
        close(listen_socket);

    LOG(ERROR)<<"Udp listen on the port "<<port<<" failed";
    return false;
}

bool EasyServer::StartOvertimeListen(OvertimeListener& ol,int timespan,overtime_cb cb,void * arg)
{
    struct event  * listen_event=NULL;
    event_base * base=NULL;
    std::shared_ptr<std::thread> listen_thread;
    do{
        try{
            base=event_base_new();
            if(!base)
                break;

            listen_event=event_new(base,-1,EV_TIMEOUT|EV_PERSIST,cb,arg);
            if(!listen_event)
                break;
            timeval tv={timespan,0};
            if(event_add(listen_event,&tv)==-1)
                break;
            listen_thread.reset(new std::thread([this,base]
                                                {
                                                    event_base_dispatch(base);
                                                }
                                    ));
        }
        catch(...){
            break;
        }

        ol.overtime_listen_base=base;
        ol.overtime_listen_event=listen_event;
        ol.overtime_listen_thread=listen_thread;

        return true;
    }while (0);
    LOG(ERROR)<<"Start the overtime listen thread failed";
    if(base)
        event_base_free(base);
    if(listen_event)
        event_free(listen_event);
    return false;
}

void EasyServer::Start()
{
    if(!factory_){
        factory_.reset(new TcpConnFactory);
        if(!factory_)
        {
            LOG(ERROR)<<"create tcp conn factory failed!";
            return;
        }
        LOG(INFO)<<"using default tcp conn factory";
    }
    else {
        LOG(INFO)<<"using selfdefined tcp conn factory";
    }

    // if(!CreateAllWorkerThreads()){
    //     LOG(ERROR)<<"Create worker threads failed";
    //     return;
    // }

    for(auto pos=vec_tcp_listeners_.begin();pos!=vec_tcp_listeners_.end();++pos)
    {
        pos->tcp_listen_thread->join();
    }

    for(auto pos=vec_udp_listeners_.begin();pos!=vec_udp_listeners_.end();++pos)
    {
        pos->udp_listen_thread->join();
    }

    for(auto pos=vec_overtime_listeners_.begin();pos!=vec_overtime_listeners_.end();++pos)
    {
        pos->overtime_listen_thread->join();
    }

    LOG(ERROR)<<"You haven't added a listener yet";
}

bool EasyServer::CreateAllWorkerThreads()
{
    bool ret=true;
    try {
        thread_pool_.reset(new ThreadPool(num_of_threads_in_threadpool_));
        for(int i=0;i<num_of_workers_;++i){
            std::shared_ptr<WorkerThread> pti(new WorkerThread(this,i));
            if(!pti->Run())
            {
                ret=false;
                break;
            }
            vec_workers_.push_back(pti);
        }
    } catch (...) {
        ret=false;
    }

    return ret;
}


void EasyServer::AcceptTcpError(evconnlistener *listener, void *ptr)
{
	//TODO
    LOG(ERROR)<<"TCP Listen Thread  Accept Error";
}

void EasyServer::AcceptTcpConn(evconnlistener * listener, int sock, sockaddr * addr, int len, void *ptr)
{
    void * ipaddr=nedalloc::nedmalloc(len);
    if(ipaddr){
        memcpy(ipaddr,addr,len);
    }

    EasyServer * es=static_cast<EasyServer *>(ptr);
    int cur_thread_index=es->GetIndexOfIdleWorker();
    SocketPort sp{sock,thread_local_port,ipaddr};

    if(!es->GetWorkerByIndex(cur_thread_index)->PushTcpConnIntoQueueAndSendNotify(sp)){
        LOG(WARNING)<<"tcp notify worker failed,close the tcp socket!";
        close(sock);
    }

}

void EasyServer::AcceptUdpConn(evutil_socket_t fd, short what, void * arg){
    LOG(DEBUG)<<"Accept udp Conn";
    EasyServer * es=static_cast<EasyServer *>(arg);
    socklen_t addr_len=sizeof(sockaddr_in);
    struct sockaddr_in * addr=NULL;
    int datalen=-1;
    void * data=NULL;
    int dupfd=-1;

    do{
        addr=(struct sockaddr_in *)nedalloc::nedcalloc(addr_len,1);
        if(!addr)
        {
            LOG(WARNING)<<"nedalloc::nedcalloc failed for udp addr";
            break;
        }
        else{
            LOG(DEBUG)<<"Allocated "<<addr_len<<" bytes data for udp addr ";
        }

        char buf[65535]={0};

        if((datalen=recvfrom(fd,buf,65535,0,(struct sockaddr *)addr,&addr_len))<=0){
            LOG(WARNING)<<"read data from udp failed";
            break;
        }

        data=nedalloc::nedmalloc(datalen);
        if(!data)
        {
            LOG(WARNING)<<"nedalloc::nedmalloc failed for udp packet";
            break;
        }
        else{
            LOG(DEBUG)<<"Allocated "<<datalen<<" bytes data for udp packet ";
        }

        memcpy(data,buf,datalen);

        dupfd=dup(fd);
        if(dupfd==-1){
            LOG(WARNING)<<"dup udp fd failed";
            break;
        }
        else{
            LOG(DEBUG)<<"dup udp fd success!"<<dupfd;
        }

        bool packethandled=false;
        for(auto pos=es->vec_udppackethandlecbs_.begin();pos!=es->vec_udppackethandlecbs_.end();++pos){
            if(pos->port==thread_local_port){
                if(pos->cb){
                    es->thread_pool_->enqueue(*pos,es,(unsigned char *)data,(int)datalen,(void *)addr,(int)addr_len,dupfd);
                    packethandled=true;
                }
            }
        }

        if(!packethandled)
            LOG(WARNING)<<"packet can't get handle cb for port "<<thread_local_port;

        return;
    }while(0);

    //error occurs
    if(addr){
        LOG(DEBUG)<<"Free "<<addr_len<<" bytes data for udp addr ";
        nedalloc::nedfree(addr);
    }

    if(data){
        LOG(DEBUG)<<"Free "<<datalen<<" bytes data for udp packet ";
        nedalloc::nedfree(data);
    }

    if(dupfd!=-1){
        LOG(DEBUG)<<"close duplicated fd for udp fd"<<dupfd;
        close(dupfd);
    }

}

void EasyServer::SendDataToTcpConnection(int threadindex,const std::string& sessionid,void * data,unsigned int len,void *arg,int arglen,bool hasResultCb)
{
    if(threadindex<0||sessionid.empty())
        return;
    std::shared_ptr<WorkerThread> worker=vec_workers_[threadindex];
    if(worker){
        char tmp[1000]={0};
        const char * hexstr=ConvertBytes2HexString((const unsigned char *)data,len,tmp,1000);
        // LOG(DEBUG)<<hexstr;

        if(!isworkerthread){//in thread pool
            SessionData sd(data,len,sessionid,arg,arglen);
            sd.hasResultCb=hasResultCb;
            if(!worker->PushDataIntoQueueAndSendNotify(sd,'d')){
                LOG(WARNING)<<"PushDataIntoQueueAndSendNotify failed";
            }
            else{
                LOG(DEBUG)<<"send data back to tcp connection from threadpool "<<sessionid<<" "<<hexstr;
            }
        }
        else{//send directly
            LOG(DEBUG)<<"send data back to tcp connection from worker "<<sessionid<<" "<<hexstr;
            worker->SendDataToTcpConnection(data,len,sessionid,arg,arglen,hasResultCb);
        }
    }
    else{
        LOG(WARNING)<<"wrong thread index";
    }
}

void EasyServer::SendDataToTcpConnection(int threadindex,const std::string& sessionid,const std::string& strdata,const std::string& strarg,bool hasResultCb)
{
    if(threadindex<0||sessionid.empty())
        return;
    std::shared_ptr<WorkerThread> worker=vec_workers_[threadindex];
    if(worker){
        char tmp[1000]={0};
        const char * hexstr=ConvertBytes2HexString((const unsigned char *)strdata.c_str(),strdata.length(),tmp,1000);
        if(!isworkerthread){//in thread pool
            SessionData sd(sessionid,strdata,strarg);
            sd.hasResultCb=hasResultCb;

            if(!worker->PushDataIntoQueueAndSendNotify(sd,'e')){
                LOG(WARNING)<<"PushDataIntoQueueAndSendNotify failed";
            }
            else{
                LOG(DEBUG)<<"send data back to tcp connection from threadpool "<<hexstr;
            }
        }
        else{//send directly
            LOG(DEBUG)<<"send data back to tcp connection from worker "<<hexstr;
            worker->SendDataToTcpConnection(sessionid,strdata,strarg,hasResultCb);
        }
    }
    else{
        LOG(WARNING)<<"wrong thread index";
    }
}

void EasyServer::CloseTcpConnection(int threadindex,const std::string& sessionid)
{
    std::shared_ptr<WorkerThread> worker=vec_workers_[threadindex];
    if(worker){
        if(!isworkerthread){//in thread pool
            SessionKill sk(sessionid);
            if(!worker->PushKillIntoQueueAndSendNotify(sk)){
                LOG(WARNING)<<"PushKillIntoQueueAndSendNotify failed";
            }
            else{
                LOG(DEBUG)<<"send kill notification to tcp connection from threadpool";
            }
        }
        else{//send directly
            LOG(DEBUG)<<"send kill notification to tcp connection from worker";
            worker->KillTcpConnection(sessionid);
        }
    }
    else{
        LOG(WARNING)<<"wrong thread index";
    }
}

std::shared_ptr<TcpConnItem> EasyServer::GetTcpConnection(int threadindex,const std::string& sessionid){
    if(threadindex<0||sessionid.empty())
        return std::shared_ptr<TcpConnItem>(nullptr);
    return vec_workers_[threadindex]->FindTcpConnItem(sessionid);
}

bool EasyServer::IsTcpConnectionExist(int threadindex,const std::string& sessionid)
{
    bool ret=true;
    do{
        if(threadindex<0||sessionid.empty()){
            ret=false;
            break;
        }
        ret=vec_workers_[threadindex]->IsTcpConnItemExist(sessionid);
    }while(0);

    return ret;
}

bool EasyServer::Init()
{
    if(!CreateAllWorkerThreads()){
        LOG(ERROR)<<"Create worker threads failed";
        return false;
    }

    return true;
}

void EasyServer::SendDataToCaptureThread(const std::string& strdata)
{
    boost::shared_lock<boost::shared_mutex> lock(mutex_un_map_capture);
    for(auto pos=un_map_capture.begin();pos!=un_map_capture.end();++pos){
        SendDataToTcpConnection(pos->second,pos->first,strdata);
    }
}

int EasyServer::GetKillQueueSize(){
    int ret=0;
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        ret+=(*pos)->GetKillSize();
    }

    return ret;
}

int EasyServer::GetDownloadQueueSize(){
    int ret=0;
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        ret+=(*pos)->GetDownloadSize();
    }
    return ret;
}

int EasyServer::GetSocketQueueSize(){
    int ret=0;
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        ret+=(*pos)->GetSocketQueueSize();
    }
    return ret;
}

int EasyServer::GetTotalSessionNum(){
    int ret=0;
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        ret+=(*pos)->GetSessionMapSize();
    }
    return ret;
}

int EasyServer::GetThreadPoolQueueSize(){
    return thread_pool_->GetQueueSize();
}

void EasyServer::ClearAllContainers(){
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        (*pos)->ClearAll();
    }

    thread_pool_->ClearQueue();
}

std::string EasyServer::GetTotalSessionString()
{
    std::string ret;
    for(auto pos=vec_workers_.begin();pos!=vec_workers_.end();++pos){
        ret+=(*pos)->GetSessionString();
    }
    return ret;
}

//reponse the cmd from the terminal
// extern void MonitorServer(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len);

extern void ReleaseCapture(TcpConnItem * tci);

bool EasyServer::AddMonitorListener(int port,tcppackethandle_cb tcpcb)
{
    TcpPacketHandleCb cb(tcpcb,port,false,true,NULL,-1,NULL,ReleaseCapture,NULL);
    if(!AddTcpListener(port,cb)){
        return false;
    }

    return true;
}
