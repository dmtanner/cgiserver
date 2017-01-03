server: server.cpp
	g++ server.cpp -lpthread -o server

clean:
	rm server
