#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>


int parse_argv(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: ./echo_server PORT\n");
		return 1;
	}

	errno = 0;

	char *endptr;
	long val = strtol(argv[1], &endptr, 10);

	if (endptr == argv[1]) {
		fprintf(stderr, "No digits were found\n");
		return 1;
	}

	if (*endptr != '\0') {
		fprintf(stderr, "Invalid characters in port\n");
		return 1;
	}

	if (errno == ERANGE) {
		perror("Port overflow or underflow");
		return 1;
	}

	if (val < 1 || val > 65535) {
		fprintf(stderr, "Port must be between 1 and 65535\n");
		return 1;
	}

	return 0;
}

int send_all(int fd, const char *buffer, size_t len) {
	size_t total = 0;

	while (total < len) {
		ssize_t sent = send(fd, buffer + total, len - total, 0);

		if (sent == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("server: send");
			return -1;
		}

		total += (size_t)sent;
	}

	return 0;
}

void handle_client(int fd) {
	char buffer[2048];

	while (true) {
		ssize_t num_bytes = recv(fd, buffer, sizeof(buffer), 0);

		if (num_bytes == 0) {
			printf("server: client disconnected\n");
			break;
		}

		if (num_bytes == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("server: recv");
			break;
		}

		if (send_all(fd, buffer, (size_t)num_bytes) == -1) {
			break;
		}
	}
}


int main(int argc, char *argv[])
{
	if (parse_argv(argc, argv) != 0) return 1;

	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *p;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	int status = getaddrinfo(NULL, argv[1], &hints, &res);

	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return 1;
	}

	int server_fd = -1;
	int yes = 1;

	for (p = res; p != NULL; p = p->ai_next) {

		server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		if (server_fd == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("server: setsockopt");
			close(server_fd);
			server_fd = -1;
			continue;
		}

		if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("server: bind");
			close(server_fd);
			server_fd = -1;
			continue;
		}

		break;
	}

	freeaddrinfo(res);

	if (p == NULL || server_fd == -1) {
		fprintf(stderr, "server: failed to bind\n");
		return 1;
	}

	if (listen(server_fd, 10) == -1) {
		perror("server: listen");
		close(server_fd);
		return 1;
	}

	printf("server: listening on port %s\n", argv[1]);

	while (true) {
		struct sockaddr_storage remote_addr;
		socklen_t addr_len = sizeof(remote_addr);

		int client_fd = accept(server_fd, (struct sockaddr*)&remote_addr, &addr_len);

		if (client_fd == -1) {
			if (errno == EINTR) {
				continue;
			}

			perror("server: accept");
			continue;
		}

		printf("server: client connected\n");

		handle_client(client_fd);

		close(client_fd);
	}

	close(server_fd);

	return 0;
}
