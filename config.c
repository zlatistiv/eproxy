#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "eproxy.h"


/*
 * string is in the format <host>:<port>
 * returns a file descriptor or -1 on fail
 */
int do_bind(char *string) {
	char *host, *port;
	int err;
	int fd;
	int yes;
	struct addrinfo hints, *res, *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;
	host = string;
	yes = 1;

	port = strrchr(string, ':');
	if (port == string)
		host = NULL;

	if (!port)
		return -1;

	*port = '\0';
	port++;

	err = getaddrinfo(host, port, &hints, &res);
	*(port-1) = ':';
	if (err != 0)
		return -1;

	for (rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == -1)
			continue;

		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);

	return fd;
}

/*
 * string is in the format tcp://<host>:<port>
 * returns a file descriptor or -1 on fail
 */
int do_upstream(char *string) {
	char *host, *port;
	int err;
	int fd;
	struct addrinfo hints, *res, *rp;
	const char *prefix = "tcp://";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (strncmp(prefix, string, strlen(prefix)) != 0)
		return -1;

	host = string + strlen(prefix);
	port = strrchr(string, ':');
	*port = '\0';
	port++;

	err = getaddrinfo(host, port, &hints, &res);
	*(port+1) = ':';
	if (err != 0)
		return -1;

	for (rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd == -1)
			    continue;

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	return fd;
}

void configure(struct config *conf, int argc, char *argv[]) {
	int opt;
	int option_index = 0;

	static struct option long_options[] = {
		{"upstream", required_argument, 0, '0'},
		{"bind",     required_argument, 0, '1'},
		{"rbsize",   required_argument, 0, '2'},
		{"header",   required_argument, 0, '3'},
		{"maxconn",  required_argument, 0, '4'},
		{"bsize",    required_argument, 0, '5'},
		{"psize",    required_argument, 0, '6'},
		{"backlog",  required_argument, 0, '7'},
		{0, 0, 0, 0}
	};

	conf->upstream_fd = UPSTREAM_FD;
	conf->rb_size = RB_SIZE;
	conf->header = HEADER;
	conf->maxconn = MAXCONN;
	conf->b_size = B_SIZE;
	conf->backlog = BACKLOG;
	conf->first_fd = conf->last_fd = 5;

	while ((opt = getopt_long_only(argc, argv, "", long_options, &option_index)) != -1) {
		switch (opt) {
			case '0':
				conf->upstream_fd = do_upstream(optarg);
				if (conf->upstream_fd == -1) {
					if (errno != 0)
						perror("upstream");
					else
						fprintf(stderr, "upstream: invalid string %s\n", optarg);
					exit(EXIT_FAILURE);
				}
				conf->first_fd = conf->last_fd = 6;
				break;
			case '1':
				conf->bind_fd = do_bind(optarg);
				if (conf->bind_fd == -1) {
					if (errno != 0)
						perror("bind");
					else
						fprintf(stderr, "bind: invalid string %s\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case '2':
				conf->rb_size = (size_t)atoll(optarg);
				break;
			case '3':
				conf->header = optarg;
				break;
			case '4':
				conf->maxconn = (unsigned int)atoi(optarg);
				break;
			case '5':
				conf->b_size = (size_t)atoll(optarg);
				break;
			case '6':
				conf->p_size = (size_t)atoll(optarg);
				break;
			case '7':
				conf->backlog = (size_t)atoll(optarg);
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
		}
	}

	if (conf->upstream_fd == 0 && conf->p_size > 0) {
		int err = fcntl(STDIN_FILENO, F_SETPIPE_SZ, conf->p_size);

		if (err < 0) {
			perror("Unable to set pipe size\n");
			exit(EXIT_FAILURE);
		}
	}

	if (conf->bind_fd == 0) {
		char *s = strdup(BIND_STRING);
		conf->bind_fd = do_bind(s);
		free(s);
		if (conf->bind_fd == -1) {
			if (errno != 0)
				perror("bind");
			else
				fprintf(stderr, "bind: invalid string %s\n", optarg);
			exit(EXIT_FAILURE);
		}

	}

	if (nonblock(conf->bind_fd) != 0)
		exit(EXIT_FAILURE);

	char *header = malloc(sizeof(conf->header));
	unescape_string(header, conf->header);
	conf->header = header;

	conf->max_events = conf->maxconn + 2;
	conf->max_fds = conf->maxconn + 4;
}
