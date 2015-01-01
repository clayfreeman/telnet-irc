telnet-irc
==========

telnet-irc is a custom, lightweight telnet client written to automatically
respond to IRC `PING` requests to avoid dropping connection.

Usage
=====

`telnet-irc <host> [port]`

To enable debug mode, recompile with `make DEBUG=1`

Examples
========
 * `telnet-irc irc.freenode.net`
 * `telnet-irc irc.example.org 6669`

Issues
======

 * When the socket is closed, `telnet-irc` doesn't close
 * My use of `libevent` could most likely be improved
