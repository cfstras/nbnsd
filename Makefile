CFLAGS += -Wall -Wno-cpp -O2 -std=c99
OUT := native

$(OUT)/nbnsd: nbnsd.c Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f nbnsd $(OUT)/nbnsd
