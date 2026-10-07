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

#define INPUT_BUFFER_CAPACITY 2048

int parse_argv(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Usage: ./echo_client ADDRESS PORT\n");
		return 1;
	}

	errno = 0;

	char *endptr;
	long val = strtol(argv[2], &endptr, 10);

	if (endptr == argv[2]) {
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

const char* inet_ntop2(void *addr, char *buf, size_t size) {
	struct sockaddr_storage* sas = addr;
	struct sockaddr_in* sa;
	struct sockaddr_in6* sa6;
	void* src;

	switch (sas->ss_family) {
		case AF_INET:
			sa = addr;
			src = &(sa->sin_addr);
			break;
		case AF_INET6:
			sa6 = addr;
			src = &(sa6->sin6_addr);
			break;
		default: return NULL;
	}

	return inet_ntop(sas->ss_family, src, buf, size);
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

int main(int argc, char* argv[]) {
	if (parse_argv(argc, argv) != 0) return 1;
	
	struct addrinfo hints;
	struct addrinfo* res;
	struct addrinfo* p;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	int status = getaddrinfo(argv[1], argv[2], &hints, &res);
		
	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s", gai_strerror(status));
		return 1;
	}

	int fd = -1;

	for (p = res; p != NULL; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

		if (fd == -1) {
			perror("client: socket");
			continue;
		}
		
		if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			close(fd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		freeaddrinfo(res);
		return 1;
	}

	char ip[INET6_ADDRSTRLEN];
	printf("connected to: %s\n", inet_ntop2(p->ai_addr, ip, INET6_ADDRSTRLEN));

	freeaddrinfo(res);
	
	char buffer[INPUT_BUFFER_CAPACITY];
	int msg_len = 0;
	while (fgets(buffer, INPUT_BUFFER_CAPACITY, stdin) != NULL) {
		if (send_all(fd, buffer, strlen(buffer)) == -1) {
			char ip[INET6_ADDRSTRLEN];
			printf("disconnected from: %s\n", ip);
			break;
		}
		if ((msg_len = recv(fd, buffer, INPUT_BUFFER_CAPACITY, 0)) == 0) continue;
		else {
			buffer[msg_len++] = '\0';
			printf("%s", buffer);
		}
	}
	
	close(fd);
	return 0;
}
