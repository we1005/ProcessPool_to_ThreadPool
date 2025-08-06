client:client.o
	gcc client.o -o client -lpthread
client.o:client.c
	gcc -c client.c -o client.o -g -Wall
clean:
	rm client.o client
