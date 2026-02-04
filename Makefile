CC = gcc
FLAGS = -Wall -Wextra

texit: texit.c
	$(CC) texit.c -o bin/texit

clean:
	rm -f *.o