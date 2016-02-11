
all: sb-client-header sb-server-header sb-code

sb-client-header:
	$(SCANNER_TOOL) client-header < simple-buffer.xml > simplebuffer-client-protocol.h

sb-server-header:
	$(SCANNER_TOOL) server-header < simple-buffer.xml > simplebuffer-server-protocol.h

sb-code:
	$(SCANNER_TOOL) code < simple-buffer.xml > simplebuffer-protocol.c

clean:
	@rm -f simplebuffer-client-protocol.h simplebuffer-server-protocol.h simplebuffer-protocol.c


