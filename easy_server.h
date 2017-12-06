/*
 * easy_server.h
 *
 *  Created on: 2016年9月28日
 *      Author: mason chen
 */
#ifndef EASY_SERVER_H_
#define EASY_SERVER_H_

#include <unordered_map>
#include<memory>
#include<mutex>
#include<thread>
#include<vector>
#include"common_structs.h"
#include"ThreadPool.h"
#include <arpa/inet.h>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>

class WorkerThread;
class EasyServer;
struct TcpPacketHandleCb;
struct UdpPacketHandleCb;

extern char * ConvertBytes2HexString(const unsigned char * bytes,int len,char * hexstr,int);

typedef void (*overtime_cb)(evutil_socket_t fd, short what,void * arg);
typedef unsigned int (*tcppacketlen_cb)(unsigned char * data,int len);
/* typedef void (*tcppackethandle_cb)(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len,bool runinthreadpool); */
typedef void (*tcppackethandle_cb)(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len);
typedef void (*udppackethandle_cb)(EasyServer * server,unsigned char * data,int len,void * addr,int addrlen,int fd);
typedef	void (*tcppacketsendresult_cb)(void  * data,int len,const std::string& sessionid,void * arg,int arglen,bool isok);
typedef	void (*tcppacketsendresult_cb2)(const std::string& sessionid,const std::string& strdata,const std::string& stragr,bool isok);
typedef void (*tcpconnclose_cb)(TcpConnItem * tci);



struct TcpListener{
	event_base * tcp_listen_base;
	evconnlistener *tcp_listener;
	std::shared_ptr<std::thread> tcp_listen_thread;
};

struct UdpListener{
	event_base * udp_listen_base;
	struct event  * udp_listen_event;
	int udp_listen_socket;
	std::shared_ptr<std::thread> udp_listen_thread;
};

struct OvertimeListener
{
	struct event  * overtime_listen_event;
	event_base * overtime_listen_base;
	std::shared_ptr<std::thread> overtime_listen_thread;
};


class TcpConnFactory{
public:
	virtual TcpConnItem * CreateTcpConn(int s,int p,int i,const std::string& sid,const std::string& ip){
		TcpConnItem * ret=new TcpConnItem(s,p,i,sid,ip);
		return ret;
	}
};


class EasyServer{
public:
	EasyServer(int num_of_workers,int num_of_threads_in_threadpool);
	~EasyServer();
	//listener add interface
	/* bool AddTcpListener(int port); */
	bool AddTcpListener(int port,TcpPacketHandleCb& tphcb);
	/* bool AddUdpListener(int port); */
	bool AddUdpListener(int port,UdpPacketHandleCb& uphcb);
	bool AddOvertimeListener(int tm,overtime_cb cb,void * arg);

	//create selfdefined tcp connection object
	void SetTcpConnItemFactory(std::shared_ptr<TcpConnFactory> f){
		factory_=f;
	}

	//init server
	bool Init();
	//start server
	void Start();

	/*send data to tcp connection*/
	/* void SendDataToTcpConnection(int threadindex,const std::string& sessionid,void * data,unsigned int len,bool runinthreadpool,void *arg=NULL,int arglen=0); */
	void SendDataToTcpConnection(int threadindex,const std::string& sessionid,void * data,unsigned int len,void *arg=NULL,int arglen=0,bool hasResultCb=true);

	void SendDataToTcpConnection(int threadindex,const std::string& sessionid,const std::string& strdata,const std::string& strarg="",bool hasResultCb=false);

	/*close tcp connection*/
	void CloseTcpConnection(int threadindex,const std::string& sessionid);

	/* //add callback for handling tcp packet */
	/* void AddTcpPacketHandleCb(TcpPacketHandleCb& hcb){ */
	/* 	vec_tcppackethandlecbs_.push_back(hcb); */
	/* } */

	/* //add callback for handling udp packet */
	/* void AddUdpPacketHandleCb(UdpPacketHandleCb& udpcb){ */
	/* 	vec_udppackethandlecbs_.push_back(udpcb); */
	/* } */

	/* //set tcp packet sending result callback */
	/* void SetTcpPacketSendResult_cb(tcppacketsendresult_cb cb){ */
	/* 	getresultcb_=cb; */
	/* } */

	/* //set tcp conn close callback */
	/* void SetTcpConnClose_cb(tcpconnclose_cb cb){ */
	/* 	tcpclosecb_=cb; */
	/* } */

	//get tcp connection by sessionid
	std::shared_ptr<TcpConnItem> GetTcpConnection(int threadindex,const std::string& sessionid);

	//judge tcp connection exists
	bool IsTcpConnectionExist(int threadindex,const std::string& sessionid);

	template<class F, class... Args>
	void InvokeFunctionUsingThreadPool(F f, Args... args)
	{
		thread_pool_->enqueue(f,args...);
	}

	static void FreeResourceByHand(unsigned char * data,int len){
		nedalloc::nedfree(data);
		LOG(DEBUG)<<"Freed "<<len<<" bytes data by hand";
	}

	//used inside,never mind!!!
	/* Tcp 处理 */
	static void AcceptTcpConn(evconnlistener *, int, sockaddr *, int, void *);
	static void AcceptTcpError(evconnlistener *, void *);
	/* Udp 处理 */
	static void AcceptUdpConn(evutil_socket_t fd, short what, void * arg);

	int GetIndexOfIdleWorker()
	{
		int cur_thread_index=0;
		{
			std::lock_guard<std::mutex>  lock(mutex_thread_index_);
			cur_thread_index = (last_thread_index_ + 1) %num_of_workers_; // 轮循选择工作线程
			last_thread_index_ = cur_thread_index;
		}

		return cur_thread_index;
	}

	std::shared_ptr<WorkerThread> GetWorkerByIndex(int index){
		return vec_workers_[index];
	}

	/* tcppacketsendresult_cb GetTcpPacketSendResult_cb(){ */
	/* 	return getresultcb_; */
	/* } */

	/* tcpconnclose_cb GetTcpConnClose_cb(){ */
	/* 	return tcpclosecb_; */
	/* } */

	int GetKillQueueSize();

	int GetDownloadQueueSize();

	int GetSocketQueueSize();

	int GetTotalSessionNum();

	int GetThreadPoolQueueSize();

	void ClearAllContainers();
private:
	bool StartTcpListen(TcpListener& tl,int port);
	bool StartUdpListen(UdpListener& ul,int port);
	bool StartOvertimeListen(OvertimeListener& ol,int timespan,overtime_cb cb,void * arg);
	bool CreateAllWorkerThreads();
private:
	int num_of_workers_;
    int num_of_threads_in_threadpool_;
	std::vector<std::shared_ptr<WorkerThread> > vec_workers_;
	std::mutex mutex_thread_index_;
	int last_thread_index_;
	std::vector<TcpListener> vec_tcp_listeners_;
	std::vector<UdpListener> vec_udp_listeners_;
	std::vector<OvertimeListener> vec_overtime_listeners_;
	tcppacketsendresult_cb getresultcb_;
	tcpconnclose_cb tcpclosecb_;
public:
	std::vector<TcpPacketHandleCb> vec_tcppackethandlecbs_;
	std::vector<UdpPacketHandleCb> vec_udppackethandlecbs_;
	std::shared_ptr<TcpConnFactory> factory_;
	std::shared_ptr<ThreadPool> thread_pool_;//线程池

	//capture map
	boost::shared_mutex mutex_un_map_capture;
	std::unordered_map<std::string,int> un_map_capture;
    void InsertCaptureIntoMap(const std::string& sid,int index){
		std::unique_lock< boost::shared_mutex > lock(mutex_un_map_capture);
		auto pos=un_map_capture.insert(std::make_pair(sid,index));
		if(!pos.second){
			pos.first->second=index;
		}
	}

    void DeleteCaptureFromMap(const std::string& sid){
		std::unique_lock< boost::shared_mutex > lock(mutex_un_map_capture);
		if(un_map_capture.erase(sid)){
			LOG(DEBUG)<<"release capture "<<sid;
		}
	}

	bool HasCapture()
	{
		boost::shared_lock<boost::shared_mutex> lock(mutex_un_map_capture);
		return !un_map_capture.empty();
	}

	std::string GetTotalSessionString();

	void SendDataToCaptureThread(const std::string& strdata);

	bool AddMonitorListener(int port,tcppackethandle_cb cb);

	/* void (int port); */
};

struct UdpPacketHandleCb{
	UdpPacketHandleCb(udppackethandle_cb c,int p,bool acf=true/* ,bool cap=true */){
		cb=c;
		port=p;
		autoclosefd=acf;
		/* captured=cap; */
	}
void operator()(EasyServer * server,unsigned char * data,int len,void * addr,int addrlen,int fd)
		{
			/* if(captured){ */
			/* 	char udpaddr[50]={0}; */
			/* 	struct in_addr * sockaddr=&(( struct sockaddr_in *)addr)->sin_addr; */
			/* 	const char * pudpaddr=inet_ntop(AF_INET,sockaddr,udpaddr,50); */
			/* 	std::string fromaddr=" from "; */
			/* 	if(pudpaddr) */
			/* 		fromaddr+=pudpaddr; */

			/* 	char tmp[10000]={0}; */
			/* 	const char * hexstr=ConvertBytes2HexString(data,len,tmp); */
			/* 	server->HandleCapture(std::string("udp packet :")+hexstr+fromaddr); */
			/* } */
			cb(server,data,len,addr,addrlen,fd);
			if(!autoclosefd){
				fd=-1;
			}
			//回收内存
			nedalloc::nedfree(data);
			LOG(DEBUG)<<"Freed "<<len<<" bytes data for udp packet";
			nedalloc::nedfree((void *)addr);
			LOG(DEBUG)<<"Freed "<<addrlen<<" bytes data for udp addr";
			if(fd!=-1)
			{
				LOG(DEBUG)<<"close udp duplicated fd "<<fd;
				close(fd);
			}
		}
	udppackethandle_cb cb;
	int port;
	bool autoclosefd;
	/* bool captured; */
};

struct TcpPacketHandleCb{
	TcpPacketHandleCb(tcppackethandle_cb hc,int p,bool tph=false,bool ar=true,tcppacketlen_cb lcb=NULL,int l=-1,tcppacketsendresult_cb rcb=NULL,tcpconnclose_cb ccb=NULL,tcppacketsendresult_cb2 rcb2=NULL/* ,bool cap=true */){
		handlecb=hc;
		lencb=lcb;
		port=p;
		threadpoolhandle=tph;
		len=l;
		autorelease=ar;
		resultcb=rcb;
		closecb=ccb;
		resultcb2=rcb2;
		/* captured=cap; */
	}
	void operator()(EasyServer * server,int threadindex,const std::string& sessionid,unsigned char * data,int len)
		{
			/* handlecb(server,threadindex,sessionid,data,len,threadpoolhandle); */
			/* if(captured){ */
			/* 	char tmp[10000]={0}; */
			/* 	const char * hexstr=ConvertBytes2HexString(data,len,tmp); */
			/* 	server->HandleCapture(std::string("tcp packet :")+hexstr); */
			/* } */
			handlecb(server,threadindex,sessionid,data,len);
			//回收内存
			if(autorelease){
				nedalloc::nedfree(data);
				LOG(DEBUG)<<"Freed "<<len<<" bytes data from session "<<sessionid;
			}

		}
	tcppackethandle_cb handlecb;
	tcppacketlen_cb lencb;
	tcppacketsendresult_cb resultcb;
	tcppacketsendresult_cb2 resultcb2;
	tcpconnclose_cb closecb;
	int port;
	int len;
	bool threadpoolhandle;
	bool autorelease;
	/* bool captured; */
};
#endif


