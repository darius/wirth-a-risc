CFLAGS := -g2 -Wall

interpret: interpret.c

clean:
	rm -f interpret *.o

.PHONY: clean
