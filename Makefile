.PHONY: all
all: client server

client: client.cpp
	g++ -Wall -Wextra -O2 -g client.cpp -o client

server: server.cpp
	g++ -Wall -Wextra -O2 -g server.cpp -o server
