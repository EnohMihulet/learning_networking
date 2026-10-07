# TCP Chat Server

I'm learning networking, and this TCP chat server in C is my first project toward that goal. It's a hands-on way to learn about sockets, TCP communication, and handling multiple clients.

## Build and run

Requires a C17 compiler and POSIX sockets (Linux or macOS).

```sh
cc -std=c17 -Wall -Wextra -Wpedantic chat_server.c -o chat_server
./chat_server 12345
```

The port must be between 1 and 65535. The server supports up to 10 simultaneous clients and uses `select()` to receive input from them.

Connect from separate terminals using netcat:

```sh
nc localhost 12345
```

## Protocol

On connection, the server sends:

```text
Welcome. Set your nickname with /NICK <name>
```

Commands are case-sensitive. Send one command or chat message per line, ending in LF (`\n`) or CRLF (`\r\n`). Multiple lines can be sent together; incomplete lines are buffered.

| Input | Response / behavior |
| --- | --- |
| `/NICK Alice` | Registers or changes your nickname; replies `OK Welcome Alice`. |
| `/WHO` | Replies `USERS Alice Bob`, listing all registered clients, including the requester, separated by spaces. With no registered clients, replies `USERS`. No extra `OK` follows. |
| `/QUIT` | Sends `BYE` and disconnects. Other clients receive no quit notification. |
| `Hello!` | Other registered clients receive `Alice: Hello!`. Register a nickname before chatting. |

Every response ends in a newline. Errors begin with `ERROR:`. `/NICK` requires exactly one argument; `/WHO` and `/QUIT` accept none. Arguments are separated by whitespace. Nicknames are unique, case-sensitive, and 1–20 characters long, accepting letters, digits, and punctuation.

Input is limited to 1024 bytes before LF (including a preceding CR, if present); command lines must additionally fit in 47 bytes after CRLF removal. An oversized line produces an error and is discarded through its newline, after which processing resumes.

## Quick checks

With the server running:

```sh
# CRLF and several commands in one write
printf '/NICK Alice\r\n/WHO\r\n/QUIT\r\n' | nc -w 2 localhost 12345

# A command split across writes
{ printf '/NICK Alice\n/WH'; sleep 1; printf 'O\n/QUIT\n'; } | nc -w 2 localhost 12345
```

On an otherwise empty server, both examples should produce the welcome instruction, `OK Welcome Alice`, `USERS Alice`, and `BYE`.
