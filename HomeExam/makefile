CFLAGS = -g -std=gnu11 -Wall -Wextra
VFLAGS = --track-origins=yes --leak-check=full --show-leak-kinds=all
start:
	@clear
	gcc $(CFLAGS) udpclient.c send_packet.c -o client
	gcc $(CFLAGS) udpserver.c pgmread.c send_packet.c -o server

runC:
	@clear
	valgrind $(VFLAGS) ./client 127.0.0.1 2020 list_of_filenames.txt 20

runS:
	@clear
	valgrind $(VFLAGS) ./server 2020 big_set output_names.txt

clean:
	rm -f client
	rm -f server

all:
	@clear
	gcc udpclient.c send_packet.c -o client
	gcc udpserver.c pgmread.c send_packet.c -o server
