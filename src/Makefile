
CC = g++
CFLAGS = -g -pthread -o
OBJ = proxy_main.o 

all:
	$(CC) -c proxy_main.cpp -std=c++11
	$(CC) $(CFLAGS) proxy_main $(OBJ) -std=c++11

clean:
	rm proxy_main *~
