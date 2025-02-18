CC = clang
CFLAGS = -Wall -Wextra -Werror -pedantic
FORMAT = clang-format -i


all: format clean memory
format:
	$(FORMAT) memory.c

memory: memory.c
	$(CC) $(CFLAGS) memory.c -o memory

clean:
	rm -f memory *.o