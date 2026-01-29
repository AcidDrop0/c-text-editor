CC = gcc
FLAGS = -Wall -Wextra

exit: exit.c
	$(CC) exit.c -o bin/exit

clean:
	rm -f *.o