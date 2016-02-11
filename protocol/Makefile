all: xdgv4 xdgv5

xdgv4: xgd-server-header-v4 xgd-code-v4

xdgv5: xgd-server-header-v5 xgd-code-v5

xgd-server-header-v4:
	$(SCANNER_TOOL) server-header < version4/xdg-shell.xml > version4/xdg-shell-server-protocol.h

xgd-code-v4:
	$(SCANNER_TOOL) code < version4/xdg-shell.xml > version4/xdg-shell-protocol.c

xgd-server-header-v5:
	$(SCANNER_TOOL) server-header < version5/xdg-shell.xml > version5/xdg-shell-server-protocol.h

xgd-code-v5:
	$(SCANNER_TOOL) code < version5/xdg-shell.xml > version5/xdg-shell-protocol.c

clean:
	@rm -f xdg-shell-server-protocol.h xdg-shell-protocol.c


