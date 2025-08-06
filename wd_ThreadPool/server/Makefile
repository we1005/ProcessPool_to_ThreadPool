server:transFile.o tcpInit.o main.o taskQueue.o tidArr.o threadPool.o epoll.o
	gcc transFile.o tcpInit.o main.o taskQueue.o tidArr.o threadPool.o epoll.o -o server -lpthread
transFile.o:transFile.c
	gcc -c transFile.c -o transFile.o -g -Wall
epoll.o:epoll.c
	gcc -c epoll.c -o epoll.o -g -Wall
threadPool.o:threadPool.c
	gcc -c threadPool.c -o threadPool.o -g -Wall
tidArr.o:tidArr.c
	gcc -c tidArr.c -o tidArr.o -g -Wall
tcpInit.o:tcpInit.c
	gcc -c tcpInit.c -o tcpInit.o -g -Wall
main.o:main.c
	gcc -c main.c -o main.o -g -Wall
taskQueue.o:taskQueue.c
	gcc -c taskQueue.c -o taskQueue.o -g -Wall
test:test.o taskQueue.o
	gcc test.o taskQueue.o -o test -lpthread
test.o:test.c
	gcc -c test.c -o test.o -g -Wall
clean:
	rm main.o tcpInit.o server
