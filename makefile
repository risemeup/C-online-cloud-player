all:server

server:*.cpp
	g++ -g -o server *.cpp -lpthread

clean:
	rm -f server