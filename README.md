# EasyServer
a  linux tcp/udp server framework

1. How To install

Download this zip file,then extract it,use ./make to compile the files ,then it will produce two files : libeasyserver.a and 
libeasyserver.so ,one for static lib and the other for dynamic. Then use ./make install to install these libs,and the path is 
/usr/local/bin.

2. run the example

After you install the libs,you can run the command below to compile the example,you also need to install libevent 2.0.22 and boost library;
g++  -std=c++11 -w main.cpp -levent -leasyserver -lboost_thread -lboost_system

