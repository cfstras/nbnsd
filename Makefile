CFLAGS += -Wall -Wno-cpp -O2 -std=c99

nbnsd: nbnsd.c Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f nbnsd
