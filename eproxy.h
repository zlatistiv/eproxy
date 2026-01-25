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

struct config {
	int upstream_fd;
	int bind_fd;
	size_t rb_size;
	char *header;
	unsigned int maxconn;
	size_t b_size;
	size_t p_size;
	size_t backlog;
	size_t max_events;
	size_t max_fds;
	int first_fd;
	int last_fd;
};


// Default config
#define UPSTREAM_FD 0
#define BIND_STRING ":::8080"
#define RB_SIZE 2072576
#define HEADER ""
#define MAXCONN 1024
#define B_SIZE 65536
#define P_SIZE 1048576
#define BACKLOG 0

int do_bind(char *string);
int do_upstream(char *string);
void configure(struct config *conf, int argc, char *argv[]);

int serve_client(int fd, struct config *conf, struct ring_buffer *rb, struct ring_buffer_sender *rbs);

static void usage() {
	fprintf(stderr,
		"-upstream\n"
		"	Upstream data source - tcp://<host>:<port>, leave empty to read from stdin.\n"
		"-bind\n"
		"	Listen address - <host>:<port> (default %s)\n"
		"-rbsize\n"
		"	Ring buffer size, (default %llu)\n"
		"-header\n"
		"	Custom header to send on each client connection. Escape sequences are interpreted. (default \"%s\")\n"
		"-maxconn\n"
		"	Maximum number of concurrent connections. (default %llu)\n"
		"-bsize\n"
		"	Read buffer size for calls to read() on the upstream. (default %llu)\n"
		"-psize\n"
		"	Override the default pipe size if upstream is a pipe. Pass 0 to do no override. (default %llu)\n"
		"-backlog\n"
		"	Start streaming with the given number of bytes behind the current position of the reader. (default %llu)\n",
	BIND_STRING, RB_SIZE, HEADER, MAXCONN, B_SIZE, P_SIZE, BACKLOG);
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
