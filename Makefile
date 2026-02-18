CC = gcc
CFLAGS = -Wall -Wextra
TARGET = bin/texit
SOURCE = texit.c

$(TARGET): $(SOURCE) | bin
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

bin:
	mkdir -p bin

clean:
	rm -f $(TARGET) *.o

.PHONY: clean bin  # Mark phony targets