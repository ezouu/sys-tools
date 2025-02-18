CC = clang
CFLAGS = -Wall -Wextra -pthread -DDEBUG -g
SRC = queue.c rwlock.c
OBJ = queue.o rwlock.o
TEST_SRC = test.c
TEST_OBJ = test.o

all: format $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

format:
	clang-format -i $(SRC) $(TEST_SRC)

clean:
	rm -f $(OBJ) $(TEST_OBJ) test

test: $(OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o test $(TEST_OBJ) $(OBJ)

.PHONY: all format clean test
