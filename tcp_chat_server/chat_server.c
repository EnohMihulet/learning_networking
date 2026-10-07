#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define BACKLOG 10
#define CLIENTS_MAX 10
#define NICKNAME_MAX_LENGTH 21
#define CLIENT_MAX_LINE_LENGTH 1024
#define CLIENT_BUFFER_CAPACITY (CLIENT_MAX_LINE_LENGTH + 1)
#define CLIENT_COMMAND_CAPACITY 48

typedef struct addrinfo addrinfo;
typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr_in6 sockaddr_in6;
typedef struct sockaddr_storage sockaddr_storage;
typedef struct sockaddr sockaddr;
typedef struct timeval timeval;

typedef struct client {
	int fd;
	char ip[INET6_ADDRSTRLEN];
	char nickname[NICKNAME_MAX_LENGTH];
	char buffer[CLIENT_BUFFER_CAPACITY];
	char command[CLIENT_COMMAND_CAPACITY];
	size_t buffer_len;
	bool discarding_line;
} client;


static client g_clients[CLIENTS_MAX];
static size_t g_clients_count = 0;
static fd_set g_master_fds, g_read_fds;
static int g_fd_max;

#define CHAT_COMMANDS \
	X(chat_command_NICK, "NICK", 1) \
	X(chat_command_WHO, "WHO", 0) \
	X(chat_command_QUIT, "QUIT", 0) \

#define X(TYPE, STRING, ARG_COUNT) TYPE,
typedef enum chat_command_type {
	CHAT_COMMANDS
	chat_command_count = 3
} chat_command_type;
#undef X

typedef struct chat_command {
	chat_command_type type;
	char* name;
	int arg_count;
} chat_command;

#define X(TYPE, STRING, ARG_COUNT) {TYPE, STRING, ARG_COUNT},
static const chat_command chat_commands[] = {
	CHAT_COMMANDS
};
#undef X

const chat_command* lookup_command_l(char* command_name, size_t command_len) {
	for (size_t i = 0; i < chat_command_count; i++) {
		if (strncmp(chat_commands[i].name, command_name, command_len) == 0 && command_len == strlen(chat_commands[i].name))
			return &chat_commands[i];
	}
	return NULL;
}

const chat_command* lookup_command(char* command_name) {
	for (size_t i = 0; i < chat_command_count; i++) {
		if (strcmp(chat_commands[i].name, command_name) == 0) return &chat_commands[i];
	}
	return NULL;
}

int parse_argv(int argc, char* argv[], int* port) {
	if (argc != 2) {
		fprintf(stderr, "Usage: ./chat_server PORT\n");
		return 1;
	}
	
	char* endptr;
	errno = 0;
	long val = strtol(argv[1], &endptr, 10);

	if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0)) {
		perror("Overflow or underflow occured");
		return 1;
	}

	if (endptr == argv[1] || *endptr != '\0') {
		fprintf(stderr, "No digits were found\n");
		return 1;
	}
	
	if (val < 1 || val > 65535) {
		fprintf(stderr, "PORT must be between 1 and 65535\n");
		return 1;
	}

	*port = val;

	return 0;
}

int get_listener(char* port) {
	int status;
	addrinfo hints, *servinfo, *p;
	int listener;

	int yes = 1;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "gai error: %s\n", gai_strerror(status));
		return -1;
	}
	
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		
		if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
			perror("setsockopt");
			close(listener);
			continue;
		}
		
		if (bind(listener, p->ai_addr, p->ai_addrlen) == -1) {
			close(listener);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind");
		freeaddrinfo(servinfo);
		return -1;
	}

	freeaddrinfo(servinfo);

	return listener;
}

int get_client_idx_by_fd(int fd, size_t* client_index) {
	if (g_clients_count == 0) return -1;
	for (size_t i = 0; i < g_clients_count; i++) {
		if (g_clients[i].fd == fd) {
			*client_index = i;
			return 0;
		}
	}
	return -1;
}

int create_client(int fd) {
	if (g_clients_count >= CLIENTS_MAX) return -1;
	memset(&g_clients[g_clients_count], 0, sizeof(client));

	g_clients[g_clients_count].fd = fd;
	g_clients_count += 1;

	return 0;
}

int delete_client(client* c) {
	if (c == NULL) return -1;

	size_t client_idx;
	if (get_client_idx_by_fd(c->fd, &client_idx) == -1) return -1;

	memmove(g_clients + client_idx, g_clients + client_idx + 1, sizeof(client) * (g_clients_count - client_idx - 1));
	g_clients_count -= 1;

	return 0;
}

const char* inet_ntop2(void *addr, char *buf, size_t size) {
	sockaddr_storage *sas = addr;
	sockaddr_in *sa4;
	sockaddr_in6 *sa6;
	void *src;

	switch (sas->ss_family) {
		case AF_INET:
			sa4 = addr;
			src = &(sa4->sin_addr);
			break;
		case AF_INET6:
			sa6 = addr;
			src = &(sa6->sin6_addr);
			break;
		default:
			return NULL;
	}

	return inet_ntop(sas->ss_family, src, buf, size);
}

int send_all(int to_fd, char* buffer, size_t* len) {
	int sent = 0;
	size_t total = 0;
	int remaining = *len;

	while (total < *len) {
		sent = send(to_fd, buffer + total, remaining, 0);
		if (sent == -1) break;
		total += sent;
		remaining -= sent;
	}

	*len = total;

	return sent == -1 ? -1 : 0;
}

int send_OK(int to_fd, char* nickname) {
	char ok[NICKNAME_MAX_LENGTH + sizeof("OK Welcome \n")];
	size_t len = snprintf(ok, sizeof(ok), "OK Welcome %s\n", nickname);
	return send_all(to_fd, ok, &len);
}

int send_BYE(int to_fd) {
	char* bye = "BYE\n";
	size_t len = strlen(bye);
	return send_all(to_fd, bye, &len);
}

int send_WELCOME(int to_fd) {
	char* welcome = "Welcome. Set your nickname with /NICK <name>\n";
	size_t len = strlen(welcome);
	return send_all(to_fd, welcome, &len);
}

int send_ERROR(int to_fd, char* msg) {
	size_t msg_len = strlen(msg);
	char* error = "ERROR";
	size_t error_len = strlen(error);
	size_t complete_msg_len = msg_len + error_len + 4;
	char complete_msg[complete_msg_len];

	complete_msg_len = snprintf(complete_msg, complete_msg_len, "%s: %s\n", error, msg);

	return send_all(to_fd, complete_msg, &complete_msg_len);
}

int handle_new_connection(int listener) {
	int new_fd;
	sockaddr_storage remote_addr;
	socklen_t addr_len = sizeof(remote_addr);
	char remoteIP[INET6_ADDRSTRLEN];

	if ((new_fd = accept(listener, (sockaddr*)&remote_addr, &addr_len)) == -1) {
		perror("server: accept");
		return -1;
	}
	
	int client_idx;
	if ((client_idx = create_client(new_fd)) == -1) {
		fprintf(stderr, "client list full");
		return -1;
	}

	g_fd_max = g_fd_max < new_fd ? new_fd : g_fd_max;
	FD_SET(new_fd, &g_master_fds);
	strcpy(g_clients[client_idx].ip, inet_ntop2(&remote_addr, remoteIP, sizeof(remoteIP)));
	printf("server: new connection from %s on socket %d\n", g_clients[client_idx].ip, new_fd);
	send_WELCOME(new_fd);
	return 0;
}

int handle_command_NICK(client* client, char* arg_start) {

	int len = 0;
	char temp_buffer[NICKNAME_MAX_LENGTH];
	for (char* c = arg_start; *c != '\0'; c++) {
		if (isalpha((unsigned char)*c) || isdigit((unsigned char)*c) || ispunct((unsigned char)*c)) {
			temp_buffer[len++] = *c;
		}
		else {
			send_ERROR(client->fd, "Nickname contains invalid character.");
			return 1;
		}
		if (len >= NICKNAME_MAX_LENGTH) break;
	}

	if (len == 0) {
		send_ERROR(client->fd, "Invalid nickname.");
		return 1;
	}
	else if (len >= NICKNAME_MAX_LENGTH) {
		send_ERROR(client->fd, "Nickname exceeds limit of 20.");
		return 1;
	}

	temp_buffer[len] = 0;
	
	for (size_t i = 0; i < g_clients_count; i++) {
		if (client->fd == g_clients[i].fd) continue;
		if (strcmp(temp_buffer, g_clients[i].nickname) == 0) {
			send_ERROR(client->fd, "Nickname is taken.");
			return 1;
		}
	}
	
	strcpy(client->nickname, temp_buffer);

	return 0;
}

int handle_command_WHO(client* client) {
	char client_names_buffer[sizeof("USERS\n") + CLIENTS_MAX * NICKNAME_MAX_LENGTH];
	size_t len = snprintf(client_names_buffer, sizeof(client_names_buffer), "USERS");
	for (size_t i = 0; i < g_clients_count; i++) {
		if (g_clients[i].nickname[0] == '\0') continue;
		len += snprintf(client_names_buffer + len, sizeof(client_names_buffer) - len, " %s", g_clients[i].nickname);
	}
	client_names_buffer[len++] = '\n';
	send_all(client->fd, client_names_buffer, &len);

	return 0;
}

int handle_command_QUIT(client* client) {

	printf("server: disconnection from %s on socket %d\n", client->ip, client->fd);

	send_BYE(client->fd);
	close(client->fd);
	FD_CLR(client->fd, &g_master_fds);
	delete_client(client);

	return -1;
}

int handle_command(client* client) {

	const chat_command* c = NULL;

	size_t len = 0;
	for (char* p = client->command + 1; *p != '\0' && !isspace((unsigned char)*p); p++) len++;

	if ((c = lookup_command_l(client->command + 1, len)) == NULL) {
		send_ERROR(client->fd, "Command not recognized.");
		return 1;
	}

	int arg_count = 0;
	char* argument = NULL;
	char* p = client->command + 1 + len;
	while (*p != '\0') {
		while (isspace((unsigned char)*p)) p++;
		if (*p == '\0') break;
		if (arg_count == 0) argument = p;
		arg_count++;
		while (*p != '\0' && !isspace((unsigned char)*p)) p++;
		if (*p != '\0') *p++ = '\0';
	}
	if (arg_count != c->arg_count) {
		send_ERROR(client->fd, "Invalid number of arguments.");
		return 1;
	}

	int err;
	switch (c->type) {
		case chat_command_NICK: err = handle_command_NICK(client, argument); break;
		case chat_command_WHO: return handle_command_WHO(client);
		case chat_command_QUIT: err = handle_command_QUIT(client); return err;
		default: return 1;
	}

	if (err == 0) send_OK(client->fd, client->nickname);

	return err;
}


int broadcast_to_named(int listener, int broadcaster, fd_set* g_master_fds, int g_fd_max, char* message, size_t len) {
	for (int i = 0; i <= g_fd_max; i++) {
		if (FD_ISSET(i, g_master_fds)) {
			if (i != listener && i != broadcaster) {
				size_t idx;
				if (get_client_idx_by_fd(i, &idx) == -1) continue;
				client* client = &g_clients[idx];
				if (client->nickname[0] != 0) send_all(i, message, &len);
			}
		}
	}

	return 0;
}


int parse_input(int listener, client* client, char* input, int input_len, fd_set* g_master_fds, int g_fd_max) {

	bool is_command = input[0] == '/';

	if (is_command) {
		if (input_len >= CLIENT_COMMAND_CAPACITY) {
			send_ERROR(client->fd, "Command too long.");
			return 1;
		}

		strcpy(client->command, input);
		return handle_command(client);
	}
	else if (client->nickname[0] != '\0') {
		size_t capacity = input_len + NICKNAME_MAX_LENGTH + 3;
		char msg[capacity];
		int len = snprintf(msg, capacity, "%s: %s\n", client->nickname, input);
		broadcast_to_named(listener, client->fd, g_master_fds, g_fd_max, msg, len);
	}
	else {
		send_ERROR(client->fd, "Must set nickname before talking in chatroom. Type /NICK <nickname>.");
	}

	return 0;
}

int recv_msg(client* client) {
	size_t space = CLIENT_BUFFER_CAPACITY - client->buffer_len;

	if (space == 0) return 2;

	int num_bytes;
	if ((num_bytes = recv(client->fd, client->buffer + client->buffer_len, space, 0)) <= 0) {
		if (num_bytes == 0) {
			printf("selectserver: %s hung up\n", client->nickname);
		}
		else {
			perror("server: recv");
		}
		return 1;
	}

	client->buffer_len += num_bytes;

	return 0;
}

int find_newline(char* buffer, size_t len) {
	for (size_t i = 0; i < len; i++) {
		if (buffer[i] == '\n') return i;
	}

	return -1;
}

int parse_client_buffer(int listener, client* client, size_t* consumed) {
	char line[CLIENT_BUFFER_CAPACITY];
	size_t start_idx = 0;

	*consumed = 0;

	for (size_t i = 0; i < client->buffer_len; i++) {
		if (client->buffer[i] != '\n') continue;

		size_t len = 0;
		for (char* c = client->buffer + start_idx; *c != '\n'; c++) {
			if (*c == '\r' && *(c+1) == '\n') continue;
			line[len++] = *c;
		}
		line[len] = 0;

		start_idx = i + 1;

		if (parse_input(listener, client, line, len, &g_master_fds, g_fd_max) < 0) return -1;
	}

	*consumed = start_idx;

	return 0;
}

void discard_buffered_input(client* client, size_t start_idx) {
	size_t remaining = client->buffer_len > start_idx ? client->buffer_len - start_idx : 0;

	if (remaining > 0) memmove(client->buffer, client->buffer + start_idx, remaining);
	memset(client->buffer + remaining, 0, client->buffer_len - remaining);
	client->buffer_len = remaining;
}

int handle_client_data(int listener, int fd) {
	client* client;
	size_t client_index;

	if (get_client_idx_by_fd(fd, &client_index) == -1) return -1;
	client = &(g_clients[client_index]);

	int recv_result = recv_msg(client);
	if (recv_result == 1) {
		printf("server: disconnection from %s on socket %d\n", client->ip, client->fd);
		delete_client(client);
		close(fd);
		FD_CLR(fd, &g_master_fds);
		return -1;
	}

	if (client->discarding_line) {
		int newline_idx = find_newline(client->buffer, client->buffer_len);
		if (newline_idx == -1) {
			discard_buffered_input(client, client->buffer_len);
			return 0;
		}

		client->discarding_line = false;
		discard_buffered_input(client, newline_idx + 1);
	}

	if (client->buffer_len == 0) return 0;

	size_t consumed = 0;
	if (parse_client_buffer(listener, client, &consumed) < 0) return 0;
	discard_buffered_input(client, consumed);

	if (client->buffer_len >= CLIENT_BUFFER_CAPACITY) {
		send_ERROR(client->fd, "Message was too long.");
		client->discarding_line = true;
		discard_buffered_input(client, client->buffer_len);
	}

	return 0;
}

int main(int argc, char* argv[]) {
	int port = 0;
	if (parse_argv(argc, argv, &port) != 0) return 1;
	
	FD_ZERO(&g_master_fds);
	FD_ZERO(&g_read_fds);

	int listener;
	char port_string[6];
	snprintf(port_string, sizeof(port_string), "%d", port);
	if ((listener = get_listener(port_string)) == -1) return 1;

	FD_SET(listener, &g_master_fds);
	g_fd_max = listener;

	if (listen(listener, BACKLOG) == -1) {
		perror("server: listen");
		return 1;
	};
	
	timeval timeout = { .tv_sec = 10, .tv_usec = 0 };

	while (true) {
		g_read_fds = g_master_fds;

		if (select(g_fd_max+1, &g_read_fds, NULL, NULL, &timeout) == -1) {
			perror("server: select");
			return 1;
		}

		for (int i = 0; i <= g_fd_max; i++) {
			if (FD_ISSET(i, &g_read_fds)) {
				if (i == listener) {
					handle_new_connection(i);
				}
				else {
					handle_client_data(listener, i);
				}
			}
		}

		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
	}

	close(listener);
	return 0;
}
