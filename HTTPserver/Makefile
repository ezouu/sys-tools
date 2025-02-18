
CC=clang
CFLAGS=-Wall -Wextra -Werror -pedantic


LINK_LIBS=asgn2_helper_funcs.a

all: httpserver

httpserver: httpserver.o
	$(CC) $(CFLAGS) -o httpserver httpserver.o $(LINK_LIBS)

# Build object files
%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f httpserver *.o
