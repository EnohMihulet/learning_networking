# Networking

I'm teaching myself computer networking by writing code instead of only reading about it. This repository holds the projects from that effort, each one small enough to finish and large enough to teach me something new about how bytes actually move between machines.

## Projects

Six projects in total

| # | Project | Status | What it teaches |
| --- | --- | --- | --- |
| 1 | [`tcp_chat_server/`](tcp_chat_server/) | done | TCP streams, `select()`, a line protocol, multiple clients |
| 2 | TBD | TBD | TBD |
| 3 | TBD | TBD | TBD |
| 4 | TBD | TBD | TBD |
| 5 | TBD | TBD | TBD |
| 6 | TBD | TBD | TBD |

Project 1 has its own README with build steps and a full protocol description:

```sh
cd tcp_chat_server
make
./chat_server 12345
```
