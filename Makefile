CXX=g++
CXXOPTIMIZE= -O2
CXXFLAGS= -g -Wall -pthread -std=c++11 $(CXXOPTIMIZE)
USERID=alex_jacob_jason
SERVERCLASSES=server.cpp
CLIENTCLASSES=client.cpp

all: server client

*.o: *.cpp
	$(CXX) -o $@ $^ $(CXXFLAGS) $@.cpp

server: $(SERVERCLASSES)
	$(CXX) -o $@ $^ $(CXXFLAGS)

client: $(CLIENTCLASSES)
	$(CXX) -o $@ $^ $(CXXFLAGS)

clean:
	rm -rf *.o *~ *.gch *.swp *.dSYM server client received.data *.tar.gz

tarball: clean
	tar -cvf $(USERID).tar.gz *
