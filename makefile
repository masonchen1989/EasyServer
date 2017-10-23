CC=g++
CFLAGS= -c -g -std=c++11 -w
objects = easy_server.o  worker_thread.o
all:$(objects) nedmalloc.o
	g++ -std=c++11 -g -w -fPIC -shared -o libeasyserver.so nedmalloc.c easy_server.cpp worker_thread.cpp
	ar crv libeasyserver.a $(objects) nedmalloc.o
$(objects): %.o: %.cpp 
	$(CC) $(CFLAGS) $< -o $@
nedmalloc.o:
	$(CC) $(CFLAGS) nedmalloc.c -o nedmalloc.o
clean:
	rm -f *.o  *.so *.a
install:
	cp *.so *.a /usr/local/lib/
