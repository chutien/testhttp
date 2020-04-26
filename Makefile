TARGET: testhttp_raw

CC 	= gcc
CFLAGS 	= -Wall -g -O2
LFLAGS 	= -Wall -g

testhttp_raw.o err.o: err.h

testhttp_raw: testhttp_raw.o err.o
	$(CC) $(LFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f testhttp_raw *.o *~ *.bak
