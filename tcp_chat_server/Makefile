CXX = clang
CXXFLAGS = -Wall -Wextra -std=c17

all: chat_server echo_server echo_client

chat_server: chat_server.o
	$(CXX) chat_server.o -o chat_server

chat_server.o: chat_server.c
	$(CXX) $(CXXFLAGS) -c chat_server.c

chat: chat_server
	rm -f chat_server.o

clean:
	rm -f *.o chat_server echo_client echo_server

echo_server: echo_server.o
	$(CXX) echo_server.o -o echo_server
	
echo_server.o: echo_server.c
	$(CXX) $(CXXFLAGS) -c echo_server.c

echos: echo_server
	rm -f echo_server.o

echo_client: echo_client.o
	$(CXX) echo_client.o -o echo_client

echo_client.o: echo_client.c
	$(CXX) $(CXXFLAGS) -c echo_client.c

echoc: echo_client
	rm -f echo_client.o

.PHONY: all clean chat echoc echos
