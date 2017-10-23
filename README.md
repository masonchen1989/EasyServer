# EasyServer
a  linux tcp/udp server framework

1. How To install

Download this zip file,then extract it,use ./make to compile the files ,then it will produce two files : libeasyserver.a and 
libeasyserver.so ,one for static lib and the other for dynamic. Then use ./make install to install these libs,and the path is 
/usr/local/bin.

2. run the example

After you install the libs,you also need to install libevent 2.0.22 and boost library,then you can run the command below to compile the example

g++  -std=c++11 -w main.cpp -levent -leasyserver -lboost_thread -lboost_system


3. APIs

3.1 tcp listener

For building a tcp listener,you need frist create a TcpPacketHandleCb instance ,


	TcpPacketHandleCb(tcppackethandle_cb hc,int p,bool tph=false,bool ar=true,tcppacketlen_cb lcb=NULL,int l=-1,tcppacketsendresult_cb rcb=NULL,tcpconnclose_cb ccb=NULL,tcppacketsendresult_cb2 rcb2=NULL){
		handlecb=hc;  //a callback that will handle the tcp data
		lencb=lcb;    //a callback that will split the tcp data into application data packet
		port=p;       //listening port for tcp listenner
		threadpoolhandle=tph;       //application packet handled by threadpool worker or receiver worker
		len=l;                      //the least num of bytes that can calculate the whole length of application data packet 
		autorelease=ar;             //the buffer that used to hold the tcp data bytes released by framework or by hand
		resultcb=rcb;               //a callback invoked when you send someting to client
		closecb=ccb;                //a callback that will be invoked when the tcp connection ends 
		resultcb2=rcb2;             //not used for now
	}

