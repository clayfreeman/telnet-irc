CC	:= cc
CFLAGS	:= -std=c11 -Wall -Wextra -pedantic
DEBUG	:= 0
DTELNET	:= /usr/bin/telnet
TELNET	:= $(shell which telnet 2>/dev/null || echo $(DTELNET))
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
	@mkdir -p $(PREFIX)/bin
	@install -m 0755 telnet-irc $(PREFIX)/bin/telnet-irc

telnet-irc: telnet-irc.c
	@echo "Compiling telnet-irc.c ..."
	@$(CC) $(CFLAGS) -o telnet-irc telnet-irc.c procmanage/procmanage.c \
	  $(FLAGS) $(LIBS)
