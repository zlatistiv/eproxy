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
 * string is in the format <host>:<port>,<header>,<backlog>
 * where header and backlog are optional
 */
int do_bind(char *string, struct listener *listener) {
	char *s, *host, *port, *header, *backlog;
	int err = 0;
	int fd;
	int yes;
	struct addrinfo hints, *res, *rp;

	host = port = header = backlog = NULL;

	s = strdup(string);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;
	host = s;
	yes = 1;

	header = strchr(s, ',');
	if (header) {
		*header = '\0';
		header++;
		if (*header != '\0') {
			backlog = strchr(header, ',');
			if (backlog) {
				*backlog = '\0';
				backlog++;
			}
		}
	}

	port = strrchr(s, ':');
	if (port == s)
		host = NULL;

	if (!port) {
		free(s);
		return -1;
	}

	*port = '\0';
	port++;

	err = getaddrinfo(host, port, &hints, &res);
	if (err != 0) {
		err = -1;
		goto out;
	}

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

	if (header) {
		listener->header = malloc(strlen(header));
		unescape_string(listener->header, header);
	} else {
		listener->header = HEADER;
	}

	if (backlog)
		listener->backlog = strtoull(backlog, NULL, 10);
	else
		listener->backlog = BACKLOG;

	if (nonblock(fd) != 0)
		err = -1;

	listener->fd = fd;

out:
	freeaddrinfo(res);
	free(s);
	return err;
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

void configure(struct config *conf, int argc, char *argv[], int first_free_fd) {
	int opt;
	int option_index = 0;
	int upstream_fd;
	int err;

	static struct option long_options[] = {
		{"upstream", required_argument, 0, 'u'},
		{"listener", required_argument, 0, 'l'},
		{"rbsize",   required_argument, 0, 'r'},
		{"maxconn",  required_argument, 0, 'm'},
		{"bsize",    required_argument, 0, 'b'},
		{"psize",    required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};

	if (argc > 1 && strcmp(argv[1], "--help") == 0) {
		usage();
		exit(0);
	}

	conf->rb_size = RB_SIZE;
	conf->maxconn = MAXCONN;
	conf->b_size = B_SIZE;
	conf->listeners = NULL;
	conf->n_listeners = 0;

	while ((opt = getopt_long_only(argc, argv, "", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'u':
				upstream_fd = do_upstream(optarg);
				if (upstream_fd == -1) {
					if (errno != 0)
						perror("upstream");
					else
						fprintf(stderr, "upstream: invalid string %s\n", optarg);
					exit(EXIT_FAILURE);
				}
				close(UPSTREAM_FD);
				dup2(upstream_fd, UPSTREAM_FD);
				close(upstream_fd);
				break;
			case 'l':
				conf->n_listeners++;
				conf->listeners = reallocarray(conf->listeners, conf->n_listeners, sizeof(struct listener));
				err = do_bind(optarg, &conf->listeners[conf->n_listeners - 1]);

				if (err == -1) {
					if (errno != 0)
						perror("bind");
					else
						fprintf(stderr, "bind: invalid string %s\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case 'r':
				conf->rb_size = (size_t)atoll(optarg);
				break;
			case 'm':
				conf->maxconn = (unsigned int)atoi(optarg);
				break;
			case 'b':
				conf->b_size = (size_t)atoll(optarg);
				break;
			case 'p':
				conf->p_size = (size_t)atoll(optarg);
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
		}
	}

	if (conf->p_size > 0) {
		err = fcntl(STDIN_FILENO, F_SETPIPE_SZ, conf->p_size);

		if (err < 0) {
			perror("Unable to set pipe size\n");
			exit(EXIT_FAILURE);
		}
	}

	if (conf->listeners == NULL) {
		conf->n_listeners++;
		conf->listeners = reallocarray(conf->listeners, conf->n_listeners, sizeof(struct listener));
		err = do_bind(BIND_STRING, &conf->listeners[conf->n_listeners - 1]);

		if (err == -1) {
			if (errno != 0)
				perror("bind");
			else
				fprintf(stderr, "bind: invalid string %s\n", optarg);
			exit(EXIT_FAILURE);
		}

	}

	conf->client_fd_range[0] = first_free_fd + conf->n_listeners;
	conf->client_fd_range[1] = -1;
	conf->max_events = conf->maxconn + first_free_fd; // 2 is for stdout and stderr
	conf->max_fds = conf->maxconn + first_free_fd;

	conf->listen_fd_range[0] = conf->listeners[0].fd;
	conf->listen_fd_range[1] = conf->listeners[conf->n_listeners - 1].fd;
}
