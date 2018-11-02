CFLAGS += -Wall -Wno-cpp -O2 -std=c99

OUT := out
PREFIX :=
SUFFIX :=

.PHONY: clean install


$(OUT)/nbnsd$(SUFFIX): nbnsd.c Makefile
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -vf nbnsd $(OUT)/*

install: $(OUT)/nbnsd
	install -m 755 $< $(PREFIX)/usr/bin
	install nbnsd.service /etc/systemd/system
	systemctl daemon-reload
	@echo 'use "systemctl enable nbnsd && systemctl start nbnsd"'
