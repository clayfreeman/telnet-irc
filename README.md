telnet-irc
==========

telnet-irc is a custom, lightweight telnet client written to automatically
respond to IRC `PING` requests to avoid dropping connection.

Compiling
=========

To compile in regular mode, type `make; make install`

To compile in debug mode, type `make DEBUG=1; make install`

Usage
=====

`telnet-irc <host> [port]`

###Examples
 * `telnet-irc irc.freenode.net`
 * `telnet-irc irc.example.org 6669`

Issues
======

 * When the socket is closed, `telnet-irc` doesn't close
 * My use of `libevent` could most likely be improved
