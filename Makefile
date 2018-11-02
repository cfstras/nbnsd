CFLAGS += -Wall -Wno-cpp -O2 -std=c99

OUT := native
PREFIX := 

.PHONY: clean install


$(OUT)/nbnsd: nbnsd.c Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f nbnsd $(OUT)/nbnsd

install: $(OUT)/nbnsd
	install -m 755 $< $(PREFIX)/usr/bin
	install nbnsd.service /etc/systemd/system
	systemctl daemon-reload
	@echo 'use "systemctl enable nbnsd && systemctl start nbnsd"'
