CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -O2 -g

.PHONY: all test clean

all: bin/database

bin/database: database.c | bin
	$(CC) $(CFLAGS) -o $@ $^

bin:
	mkdir -p bin

test: bin/database
	./tests/run_tests.sh

clean:
	rm -rf bin database.dat database.idx
