CFLAGS = -g3 -Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pedantic -std=gnu99 -Werror

EXECS = 33sh 33noprompt # All executables to make
PROMPT = -DPROMPT
CC = gcc

.PHONY = all clean

all: $(EXECS)

33sh: sh.c jobs.c
	# compile with -DPROMPT macro
	$(CC) $(CFLAGS) $(PROMPT) $^ -o $@

33noprompt: sh.c jobs.c
	# compile without the prompt macro
	$(CC) $(CFLAGS) $^ -o $@

clean:
	# clean up any executable files that this Makefile has produced
	rm -f $(EXECS)
