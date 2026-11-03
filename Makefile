CC = cc
CFLAGS = -O2 -Wall -Wextra

abi_check: abi_check.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f abi_check
