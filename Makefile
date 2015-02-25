
CC=gcc
CFLAGS=-g 
SRC=$(wildcard *.c)
OBJS=$(SRC:%.c=%.o)
EXECS=$(patsubst %.c, %, $(SRC))

.PHONY: clean all

all: $(OBJS)

%.o : %.c
	$(CC) $(CFLAGS) -o $(patsubst %.o,%,$@) $<

clean:
	rm -f $(EXECS)



