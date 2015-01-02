CC	:= cc
CFLAGS	:= -std=c11 -Wall -Wextra -pedantic
DEBUG	:= 0
TELNET	:= $(shell which telnet 2>/dev/null || echo /usr/bin/telnet)
FLAGS	:= -DDEBUG=$(DEBUG) -DTELNET=\"$(TELNET)\"
LIBS	:= -levent
PREFIX	:= /usr/local

.PHONY: all install

all: telnet-irc

clean:
	@echo "Cleaning up ..."
	@rm -f telnet-irc

install: telnet-irc
	@echo "Installing telnet-irc to $(PREFIX)/bin ..."
	@install -m 0755 telnet-irc $(PREFIX)/bin

telnet-irc:
	@echo "Compiling telnet-irc.c ..."
	@$(CC) $(CFLAGS) -o telnet-irc telnet-irc.c $(FLAGS) $(LIBS)
