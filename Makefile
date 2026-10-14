CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g
SRC := src/board.c src/ai.c src/network.c src/main.c
HDRS := include/board.h include/ai.h include/network.h
BIN := klosnowo

.PHONY: all clean run

all: $(BIN)

$(BIN): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN)

run: all
	./$(BIN)

clean:
	rm -f $(BIN)
