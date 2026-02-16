#ifndef EPROXY_H
#define EPROXY_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

struct ring_buffer {
	char *data;
	size_t size;
	size_t pos;
	unsigned long long bytes_read;
};

struct ring_buffer_sender {
	size_t pos;
	unsigned long long bytes_sent;
	size_t start_pos;
	char host[INET6_ADDRSTRLEN];
	int port;
};

struct listener {
	int fd;
	char *header;
	size_t backlog;
};

struct config {
	struct listener *listeners;
	size_t n_listeners;
	size_t rb_size;
	unsigned int maxconn;
	size_t b_size;
	size_t p_size;
	size_t max_events;
	size_t max_fds;
	int client_fd_range[2];
	int listen_fd_range[2];
};


#define UPSTREAM_FD 0
// Default config
#define BIND_STRING ":::8080"
#define RB_SIZE 2072576
#define HEADER ""
#define MAXCONN 1024
#define B_SIZE 65536
#define P_SIZE 1048576
#define BACKLOG 0

int do_bind(char *string, struct listener *listener);
int do_upstream(char *string);
void configure(struct config *conf, int argc, char *argv[], int first_free_fd);

int serve_client(int fd, struct ring_buffer *rb, struct ring_buffer_sender *rbs);

static void usage() {
	fprintf(stderr,
		"-u, --upstream\n"
		"	Upstream data source - tcp://<host>:<port>, leave empty to read from stdin.\n"
		"-l, --listener\n"
		"	Listen address - <host>:<port>,<header>,<backlog> (default %s)\n"
		"	header is a custom string that will be sent to each cient when\n"
		"	they connect, escape sequences will be interpreted.\n"
		"	passing the backlog parameter will start streaming\n"
		"	with the given number of bytes behind the current position.\n"
		"	header and backlog can be omitted.\n"
		"	More than one listener can be defined.\n"
		"-r, --rbsize\n"
		"	Ring buffer size, (default %llu)\n"
		"-m, --maxconn\n"
		"	Maximum number of concurrent connections. (default %llu)\n"
		"-b, --bsize\n"
		"	Read buffer size for calls to read() on the upstream. (default %llu)\n"
		"-p, --psize\n"
		"	Override the default pipe size if upstream is a pipe. Pass 0 to do no override. (default %llu)\n",
	BIND_STRING, RB_SIZE, MAXCONN, B_SIZE, P_SIZE);
}

static int nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);

	if (flags == -1) {
		perror("fcntl");
		return -1;
	}

	flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (flags == -1) {
		perror("fcntl");
		return -1;
	}

	return 0;
}

static ssize_t send_all(int fd, char *buf, size_t len) {
	ssize_t n;
	size_t sent;
	
	n = sent = 0;

	while (sent < len) {
		n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EAGAIN)
				return sent;
			else
				return -1;
		}
		sent += n;
	}
	return sent;
}

static void unescape_string(char* dest, const char* src) {
	while (*src) {
		if (*src == '\\') {
			src++;
			switch (*src) {
				case 'n':  *dest++ = '\n'; break;
				case 't':  *dest++ = '\t'; break;
				case 'r':  *dest++ = '\r'; break;
				case 'b':  *dest++ = '\b'; break;
				case 'f':  *dest++ = '\f'; break;
				case '\\': *dest++ = '\\'; break;
				case '\"': *dest++ = '\"'; break;
				case '\'': *dest++ = '\''; break;
				case '0':  *dest++ = '\0'; break;
				default:   *dest++ = *src;   break;
			}
		} else {
			*dest++ = *src;
		}
		src++;
	}
	*dest = '\0';
}


#define MIN(a, b) (( (a) < (b) ) ? (a) : (b))
#define MAX(a, b) (( (a) > (b) ) ? (a) : (b))

#endif
