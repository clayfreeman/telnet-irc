telnet-irc
==========

telnet-irc is a custom, lightweight telnet wrapper written to automatically
respond to IRC `PING` requests to avoid dropping connection.

Compiling
=========

telnet-irc depends on
 * A UNIX-based operating system
 * `libevent-dev` (at least on Debian-based distros)
 * `telnet` (a common command on most systems)
 * `which` (if your telnet isn't located at `/usr/bin/telnet`)

To compile in regular mode, type `make; make install`

To compile in debug mode, type `make DEBUG=1; make install`

To install to a different prefix, `make; make install PREFIX=/path/to/prefix`

If for some reason you don't have the `which` command and your telnet isn't
located at `/usr/bin/telnet`, you can compile with
`make TELNET=/path/to/telnet; make install`

Usage
=====

`telnet-irc <host> [port]`

###Examples
 * `telnet-irc irc.freenode.net`
 * `telnet-irc irc.example.org 6669`

Plans
=====

Upcoming plans for this utility include:

 * Automatic registration (will use nick/ident from command-line)
 * SSL support (will use an alternate SSL enabled telnet; ex. gnutls-cli)
