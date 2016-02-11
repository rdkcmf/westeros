
all: client-header server-header code

client-header:
	$(SCANNER_TOOL) client-header < simpleshell.xml > simpleshell-client-protocol.h

server-header:
	$(SCANNER_TOOL) server-header < simpleshell.xml > simpleshell-server-protocol.h

code:
	$(SCANNER_TOOL) code < simpleshell.xml > simpleshell-protocol.c

clean:
	@rm -f simpleshell-client-protocol.h simpleshell-server-protocol.h simpleshell-protocol.c simpleshell-protocol.o


