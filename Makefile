CC = gcc
FLAGS = -Wall -Wextra

exit: exit.c
	$(CC) texit.c -o bin/texit

clean:
	rm -f *.o