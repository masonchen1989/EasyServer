/*
 * worker_thread.cpp
 *
 *  Created on: 2016年9月29日
 *      Author: mason chen
 */
#include "worker_thread.h"
#include <unistd.h>
#include <fcntl.h>
#include"easy_server.h"

thread_local WorkerThread *thread=NULL;
extern thread_local bool isworkerthread;



char ConvertNumToHexChar3(unsigned char num)
{
    char ret=-1;
    if(num>=0&&num<=9)
        ret=num+'0';
    else if(num>=10&&num<=15){
        ret=num+'A'-10;
    }

    return ret;
}

char * ConvertBytes2HexString2(const unsigned char * bytes,int len,char * hexstr)
{
    for(int i=0;i<len;++i){
        hexstr[2*i]=ConvertNumToHexChar3(bytes[i]>>4);
        hexstr[2*i+1]=ConvertNumToHexChar3(bytes[i]&0x0f);
    }

    hexstr[2*len]='\0';
    return hexstr;
}

WorkerThread::WorkerThread(EasyServer * es,int i)
{
	pthread_event_base_=NULL;
	pnotify_event_=NULL;
    // ptimeout_event_=NULL;
	notfiy_recv_fd_=-1;
    notfiy_send_fd_=-1;
    es_=es;
    threadindex_=i;
}

WorkerThread::~WorkerThread()
{
	if(notfiy_recv_fd_!=-1)
		close(notfiy_recv_fd_);
	if(notfiy_send_fd_!=-1)
		close(notfiy_send_fd_);
	if(pthread_event_base_!=NULL)
		event_base_free(pthread_event_base_);
	if(pnotify_event_!=NULL)
		event_free(pnotify_event_);
    // if(ptimeout_event_)
    //     event_free(ptimeout_event_);
}

bool WorkerThread::Run()
{
	do{
		if(!CreateNotifyFds())
			break;
		if(!InitEventHandler())
			break;
		try{
            ptr_thread_.reset(new std::thread([this]
													 {
                                                         thread=this;//thread_local variable
                                                         isworkerthread=true;
														 event_base_loop(pthread_event_base_, 0);
													 }
										 ));
		}catch(...)
		{
			break;
		}
		return true;
	}while(0);
	return false;
}
bool WorkerThread::CreateNotifyFds()
{
	 int fds[2];
	 bool ret=false;
     if (!pipe2(fds,O_NONBLOCK))
	 {
		  notfiy_recv_fd_= fds[0];
		  notfiy_send_fd_ = fds[1];
		  ret=true;
	 }
	 return ret;
}

bool WorkerThread::InitEventHandler()
{
		do
		{
			pthread_event_base_=event_base_new();
			if(pthread_event_base_==NULL)
				break;
            pnotify_event_=event_new(pthread_event_base_,notfiy_recv_fd_,EV_READ | EV_PERSIST,HandleNotifications,(void *)this);
			if(pnotify_event_==NULL)
				break;
			if(event_add(pnotify_event_, 0))
				break;

			return true;
		}while(0);
		return false;
}

void WorkerThread::HandleNotifications(evutil_socket_t fd, short what, void* arg)
{
    WorkerThread* pwt=static_cast<WorkerThread *>(arg);
    char  buf[1];
    if(read(fd, buf, 1)!=1)//从sockpair的另一端读数据
    {
        LOG(WARNING)<<"Worker accept notification failed";
        return;
    }

    if(buf[0]=='t')//tcp connection
        HandleTcpConn(pwt);
    else if(buf[0]=='d'){
        SessionData sd=pwt->PopDataFromQueue();
        pwt->SendDataToTcpConnection(sd.data,sd.len,sd.sessionid,sd.arg,sd.arglen,sd.hasResultCb);
    }
    else if(buf[0]=='e'){
        SessionData sd=pwt->PopDataFromQueue();
        pwt->SendDataToTcpConnection(sd.sessionid,sd.strdata,sd.strarg,sd.hasResultCb);
    }
    else if(buf[0]=='k'){
        SessionKill sk=pwt->PopKillFromQueue();
        pwt->KillTcpConnection(sk.sessionid);
    }
    else{
        LOG(WARNING)<<"unkonwn notification";
    }
}


void WorkerThread::HandleTcpConn(WorkerThread* pwt)
{
    struct bufferevent * bev=NULL;
    do{
        SocketPort sp=pwt->PopTcpConnFromQueue();
        //generate sessionid
        int ts=time(0);
        if(ts==-1)
            break;
        char sessionid[50]={0};
        snprintf(sessionid,50,"%d:%d",ts,sp.sock);

        //allocate buffer for socket
        bev=bufferevent_socket_new(pwt->pthread_event_base_,sp.sock,BEV_OPT_CLOSE_ON_FREE);
        if(bev==NULL)
            break;

        std::shared_ptr<TcpConnItem> ptci(pwt->es_->factory_->CreateTcpConn(sp.sock,sp.port,pwt->threadindex_,sessionid));

        if(!ptci)
            break;
        ptci->buff=bev;

        pwt->InsertTcpConnItem(ptci);

        bufferevent_setcb(bev, TcpConnReadCb, NULL/*ConnWriteCb*/, TcpConnEventCB,ptci.get());
        bufferevent_enable(bev, EV_READ /*| EV_WRITE*/ );
        LOG(DEBUG)<<"got an tcp connection and session id is "<<ptci->sessionid;
        return;

    }while(0);

    //error occurred
    LOG(WARNING)<<"something wrong happened about the tcp connection";
    if(bev)
        bufferevent_free(bev);

}

void WorkerThread::TcpConnReadCb(bufferevent * bev,void *ctx){
    TcpConnItem * ptci=static_cast<TcpConnItem *>(ctx);
    struct evbuffer * in=bufferevent_get_input(bev);
    struct evbuffer * out=bufferevent_get_output(bev);

    size_t buffer_length=evbuffer_get_length(in);

    LOG(DEBUG)<<"Got "<<buffer_length<<" bytes data from "<<ptci->sessionid<<" tcp connection";
    int threadindex=ptci->threadindex;
    std::string sessionid=ptci->sessionid;

    while (buffer_length){
        if(ptci->remaininglength){
            if(!ptci->AllocateCopyData(in,&buffer_length)){
                LOG(WARNING)<<"AllocateCopyData failed";
                ptci->FreeData();
                evbuffer_drain(in,65535);//clear the buffer
                return;
            }

            //has received an complete packet
            if(!ptci->remaininglength){
                unsigned char * data=(unsigned char *)ptci->data;
                unsigned int len=ptci->totallength;
                ptci->ReleaseDataOwnership();//释放所有权

                if(ptci->handlefunindex!=-1){
                    if(thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex].threadpoolhandle){
                        LOG(DEBUG)<<"packet handled by threadpool";
                        thread->es_->thread_pool_->enqueue(thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex],thread->es_,ptci->threadindex,ptci->sessionid,data,len);
                    }else{
                        LOG(DEBUG)<<"packet handled by worker thread";
                        (thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex])(thread->es_,ptci->threadindex,ptci->sessionid,data,len);
                    }

                    if(!thread->es_->IsTcpConnectionExist(threadindex,sessionid)){
                        break;
                    }
                }
                else{
                    LOG(ERROR)<<"Can't be here! There must be something wrong!";
                }

            }
        }
        else{
            //calculate tcp packet length
            std::vector<TcpPacketHandleCb>::iterator handlecb=thread->es_->vec_tcppackethandlecbs_.end();
            int packetlen=-1;
            bool threadpoolhandle=false;
            for(auto pos=thread->es_->vec_tcppackethandlecbs_.begin();pos!=thread->es_->vec_tcppackethandlecbs_.end();++pos)
            {
                if(pos->port==ptci->port){
                    //judge the length
                    if((int)buffer_length<pos->len){
                        packetlen=-2;//the length is too short
                        break;
                    }

                    if(pos->lencb){
                        unsigned char ch[65535]={0};
                        if(evbuffer_copyout(in,ch,pos->len)!=-1){
                            if((packetlen=(pos->lencb)(ch,pos->len))>0){
                                // handlecb=pos->handlecb;
                                handlecb=pos;
                                threadpoolhandle=pos->threadpoolhandle;
                                break;
                            }
                            else{
                                //TODO
                                char tmp[200]={0};
                                ConvertBytes2HexString2(ch,pos->len,tmp);
                                LOG(WARNING)<<"buffer length now is "<<buffer_length<<" and bytes are "<<tmp;

                                packetlen=-1;
                            }
                        }
                        else{
                            packetlen=-1;
                            LOG(ERROR)<<"evbuffer_copyout data failed from "<<ptci->sessionid<<" tcp connection";
                            break;
                        }
                    }
                    else{//has no application format,it matchs every packet
                        packetlen=-1;
                        // handlecb=pos->handlecb;
                        handlecb=pos;
                        threadpoolhandle=pos->threadpoolhandle;
                        break;
                    }

                }
            }

            if(packetlen==-2){
                LOG(WARNING)<<"The length is too short";
                break;
            }

            if(handlecb==thread->es_->vec_tcppackethandlecbs_.end()){
                LOG(WARNING)<<"packet can't get handle cb for tcp port "<<ptci->port;
                evbuffer_drain(in,65535);//clear the buffer
                return;
            }
            else{
                ptci->handlefunindex=handlecb-thread->es_->vec_tcppackethandlecbs_.begin();
            }

            if(packetlen==-1){
                LOG(DEBUG)<<"the tcp packet has no application protocol";
                packetlen=buffer_length;
            }
            else{
                LOG(DEBUG)<<"the tcp packet has application protocol";
            }

            if(!ptci->AllocateCopyData(in,&buffer_length,packetlen)){
                LOG(WARNING)<<"AllocateCopyData failed";
                ptci->FreeData();
                evbuffer_drain(in,65535);//clear the buffer
                return;
            }
            else{
                LOG(DEBUG)<<"Allocated "<<packetlen<<" bytes data for session "<<ptci->sessionid;
            }

            //has received an complete packet
            if(!ptci->remaininglength){
                unsigned char * data=(unsigned char *)ptci->data;
                unsigned int len=ptci->totallength;
                ptci->ReleaseDataOwnership();//释放所有权

                if(ptci->handlefunindex!=-1){
                    if(thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex].threadpoolhandle){
                        LOG(DEBUG)<<"packet handled by threadpool";
                        thread->es_->thread_pool_->enqueue(thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex],thread->es_,ptci->threadindex,ptci->sessionid,data,len);
                    }else{
                        LOG(DEBUG)<<"packet handled by worker thread";
                        (thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex])(thread->es_,ptci->threadindex,ptci->sessionid,data,len);
                    }

                    if(!thread->es_->IsTcpConnectionExist(threadindex,sessionid)){
                        break;
                    }
                }
                else{
                    LOG(ERROR)<<"Can't be here! There must be something wrong!";
                }
            }

        }
    }

        
}

void WorkerThread::TcpConnEventCB(bufferevent *bev,short int  events,void * ctx){
    //TODO

    TcpConnItem * ptci=static_cast<TcpConnItem *>(ctx);
    LOG(DEBUG)<<"tcp conn got an event "<<ptci->sessionid;
    // tcpconnclose_cb closecb=thread->es_->GetTcpConnClose_cb();
    if(ptci->handlefunindex!=-1){
        tcpconnclose_cb closecb=thread->es_->vec_tcppackethandlecbs_[ptci->handlefunindex].closecb;
        if(closecb){
            closecb(ptci);
        }
    }

    thread->DeleteTcpConnItem(ptci->sessionid);
    // bufferevent_free(bev);

}

void WorkerThread::SendDataToTcpConnection(void * data,int len,const std::string& sessionid,void *arg,int arglen,bool hasResultCb){
    bool ret=false;
    std::lock_guard<std::recursive_mutex>  lock(mutex_un_map_tcp_conns_);
    auto ptr=un_map_tcp_conns_.find(sessionid);
    if(ptr!=un_map_tcp_conns_.end()){
        if(bufferevent_write(ptr->second->buff,data,len)!=-1){
            ret=true;
        }
        // if(write(ptr->second->sock,data,len)!=-1){
        //     ret=true;
        // }

        //invoke the result get cb
        // tcppacketsendresult_cb getcb=es_->GetTcpPacketSendResult_cb();
        if(hasResultCb){
            if(ptr->second->handlefunindex!=-1){
                tcppacketsendresult_cb getcb=thread->es_->vec_tcppackethandlecbs_[ptr->second->handlefunindex].resultcb;
                if(getcb)
                    getcb(data,len,sessionid,arg,arglen,ret);
                else{
                    LOG(WARNING)<<"You have not set a get result callback for sending packet!";
                }
            }
        }

    }

}

void WorkerThread::SendDataToTcpConnection(const std::string& sessionid,const std::string& strdata,const std::string& strarg,bool hasResultCb)
{
    bool ret=false;
    std::lock_guard<std::recursive_mutex>  lock(mutex_un_map_tcp_conns_);
    auto ptr=un_map_tcp_conns_.find(sessionid);
    if(ptr!=un_map_tcp_conns_.end()){
        if(bufferevent_write(ptr->second->buff,strdata.c_str(),strdata.length())!=-1){
            ret=true;
        }
        // if(write(ptr->second->sock,data,len)!=-1){
        //     ret=true;
        // }

        //invoke the result get cb
        // tcppacketsendresult_cb getcb=es_->GetTcpPacketSendResult_cb();
        if(hasResultCb){
            if(ptr->second->handlefunindex!=-1){
                tcppacketsendresult_cb2 getcb=thread->es_->vec_tcppackethandlecbs_[ptr->second->handlefunindex].resultcb2;
                if(getcb)
                    getcb(sessionid,strdata,strarg,ret);
                else{
                    LOG(WARNING)<<"You have not set a get result callback for sending packet!";
                }
            }
        }

    }
}

void WorkerThread::KillTcpConnection(const std::string& sessionid)
{
    std::lock_guard<std::recursive_mutex>  lock(mutex_un_map_tcp_conns_);
    auto ptr=un_map_tcp_conns_.find(sessionid);
    if(ptr!=un_map_tcp_conns_.end()){
        if(ptr->second->handlefunindex!=-1){
            tcpconnclose_cb closecb=thread->es_->vec_tcppackethandlecbs_[ptr->second->handlefunindex].closecb;
            if(closecb){
                closecb(ptr->second.get());
            }
        }

        // bufferevent_free(ptr->second->buff);
        DeleteTcpConnItem(sessionid);
        LOG(DEBUG)<<"Tcp connection "<<sessionid<<" killed";
    }
}

// void MonitorServer(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len)
// {
//     thread->es_->InsertCaptureIntoMap(sessionid,threadindex);
//     LOG(DEBUG)<<"insert capture "<<sessionid;
// }

void ReleaseCapture(TcpConnItem * tci)
{
    thread->es_->DeleteCaptureFromMap(tci->sessionid);
}
