#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <signal.h>
#include "eproxy.h"


void stat_client(int fd, struct ring_buffer_sender *rbs) {
	struct tcp_info info;
	socklen_t len = sizeof(info);
	fprintf(stderr, "Closing connection from %s:%d, total bytes sent: %llu, total tcp retransmissions: ", rbs->host, rbs->port, rbs->bytes_sent);
	if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0)
		fprintf(stderr, "%u\n", info.tcpi_total_retrans);
	else {
		fprintf(stderr, "unknown\n");
		perror("getsockopt");
	}
}

void read_upstream(struct config *conf, struct ring_buffer *rb) {
	ssize_t n;

	n = read(UPSTREAM_FD, rb->data + rb->pos, conf->b_size);
	if (n < 0) {
		perror("read");
		exit(EXIT_FAILURE);
	}

	rb->pos += n;
	if (rb->pos > rb->size) {
		memcpy(rb->data, rb->data + rb->size, rb->pos - rb->size);
		rb->pos %= rb->size;
	}
	rb->bytes_read += n;
}

int accept_client(struct listener *listener, struct config *conf, struct ring_buffer *rb, struct ring_buffer_sender *rbs[], int epoll_fd) {
	struct sockaddr_storage client_addr;
	socklen_t addr_size = sizeof(client_addr);
	struct epoll_event ev;
	char host[INET6_ADDRSTRLEN];
	int port;
	int client_fd;

	client_fd = accept(listener->fd, (struct sockaddr *)&client_addr, &addr_size);

	if (client_fd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return -1;
		else {
			perror("accept");
			return -1;
		}
	}

	if (nonblock(client_fd) != 0) {
		close(client_fd);
		return -1;
	}

	if (client_addr.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
		inet_ntop(AF_INET, &s->sin_addr, host, sizeof(host));
		port = ntohs(s->sin_port);
	} else {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&client_addr;

		if (IN6_IS_ADDR_V4MAPPED(&s->sin6_addr)) {
			struct in_addr ipv4addr;
			memcpy(&ipv4addr, &s->sin6_addr.s6_addr[12], sizeof(ipv4addr));
			inet_ntop(AF_INET, &ipv4addr, host, sizeof(host));
		}
		else {
			inet_ntop(AF_INET6, &s->sin6_addr, host, sizeof(host));
		}

		port = ntohs(s->sin6_port);
	}
	
	if (client_fd <= conf->max_fds)
		fprintf(stderr, "Received connection from %s:%d\n", host, port);
	else {
		fprintf(stderr, "Rejecting connection from %s:%d due to max client limit\n", host, port);
		close(client_fd);
		return -1;
	}

	ev.events = EPOLLOUT | EPOLLET | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
	ev.events = EPOLLOUT;
	ev.data.fd = client_fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
		perror("epoll_ctl");
		close(client_fd);
		return -1;
	}


	if (rbs[client_fd] == NULL) {
		rbs[client_fd] = malloc(sizeof(struct ring_buffer_sender));
		if (!rbs[client_fd]) {
			perror("malloc");
			close(client_fd);
		}
	}
	memset(rbs[client_fd], 0, sizeof(struct ring_buffer_sender));

	rbs[client_fd]->pos = rb->pos;

	size_t backlog = MIN(rb->bytes_read, listener->backlog);
	rbs[client_fd]->start_pos = (rb->pos + rb->size - backlog) % rb->size;

	ssize_t n;
	n = send_all(client_fd, listener->header, strlen(listener->header));
	if (n < 0) {
		fprintf(stderr, "Unable to send header to %s:%d, closing connection.\n", host, port);
		close(client_fd);
	}
	rbs[client_fd]->bytes_sent += n;
	strncpy(rbs[client_fd]->host, host, INET6_ADDRSTRLEN);
	rbs[client_fd]->port = port;

	return client_fd;
}

int serve_client(int fd, struct ring_buffer *rb, struct ring_buffer_sender *rbs) {
	ssize_t n;

	if (rbs->pos <= rb->pos) {
		n = send_all(fd, rb->data + rbs->pos, rb->pos - rbs->pos);
		if (n < 0)
			goto close;
		rbs->pos += n;
		rbs->bytes_sent += n;
	}
	else {
		n = send_all(fd, rb->data + rbs->pos, rb->size - rbs->pos);
		if (n < 0)
			goto close;
		rbs->pos += n;
		rbs->bytes_sent += n;
		if (rbs->pos >= rb->size) {
			rbs->pos = 0;
			n = send_all(fd, rb->data + rbs->pos, rb->pos - rbs->pos);
			if (n < 0)
				goto close;
			rbs->pos += n;
			rbs->bytes_sent += n;
		}
	}

	return 0;
close:
	stat_client(fd, rbs);
	close(fd);
	return -1;
}

volatile sig_atomic_t running = 1;
void do_sigint(int sig) {
	(void)sig;
	running = 0;
}

int main(int argc, char* argv[]) {
	struct sigaction sa = {0};
	struct config conf;
	struct epoll_event ev, *events;
	struct ring_buffer *rb;
	struct ring_buffer_sender **rbs;
	int epoll_fd;
	int err;

	sa.sa_handler = do_sigint;
	sigaction(SIGINT, &sa, NULL);

	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		perror("epoll_create");
		exit(EXIT_FAILURE);
	}

	memset(&conf, 0, sizeof(struct config));
	configure(&conf, argc, argv, epoll_fd + 1);

	rbs = calloc(conf.max_fds, sizeof(void *));
	if (!rbs) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	events = (struct epoll_event *)calloc(conf.max_events, sizeof(struct epoll_event));
	if (!events) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLIN;
	ev.data.fd = UPSTREAM_FD;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, UPSTREAM_FD, &ev);
	if (err != 0) {
		perror("epoll_ctl");
		exit(EXIT_FAILURE);
	}

	rb = calloc(1, sizeof(struct ring_buffer));
	if (!rb) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	rb->data = calloc(conf.rb_size + conf.b_size, sizeof(char));
	if (!rb->data) {
		perror("calloc");
		exit(EXIT_FAILURE);
	}
	rb->size = conf.rb_size;
	rb->pos = 0;
	rb->bytes_read = 0;

	for (size_t i = 0; i < conf.n_listeners; i++) {
		if (listen(conf.listeners[i].fd, 128) < 0) {
			perror("listen");
			exit(EXIT_FAILURE);
		}

		ev.data.fd = conf.listeners[i].fd;
		epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conf.listeners[i].fd, &ev);
		if (err != 0) {
			perror("epoll_ctl");
			exit(EXIT_FAILURE);
		}
	}

	while (running) {
		int nfds = epoll_wait(epoll_fd, events, conf.max_events, -1);
		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == UPSTREAM_FD) {
				read_upstream(&conf, rb);
				for (int fd = conf.client_fd_range[0]; fd <= conf.client_fd_range[1]; fd++) {
					if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
						ev.events = EPOLLOUT | EPOLLET | EPOLLHUP | EPOLLRDHUP | EPOLLERR;
						ev.data.fd = fd;
						err = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
						if (err != 0) {
							perror("epoll_ctl");
						}
					}
				}
			}
			else if (events[i].data.fd <= conf.listen_fd_range[1]) {
				int fd;
				fd = accept_client(&conf.listeners[events[i].data.fd - conf.listen_fd_range[0]], &conf, rb, rbs, epoll_fd);
				conf.client_fd_range[1] = MAX(fd, conf.client_fd_range[1]);
			}
			else if (rbs[events[i].data.fd]) {
				err = serve_client(events[i].data.fd, rb, rbs[events[i].data.fd]);
				if (errno != EAGAIN && err != -1) {
					ev.events = EPOLLHUP | EPOLLRDHUP | EPOLLERR;
					ev.data.fd = events[i].data.fd;
					err = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, events[i].data.fd, &ev);
					if (err != 0) {
						perror("epoll_ctl");
					}
				}
			}
		}
	}

	for (int fd = conf.client_fd_range[0]; fd <= conf.client_fd_range[1]; fd++) {
		if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
			stat_client(fd, rbs[fd]);
			close(fd);
		}
	}
	fprintf(stderr, "Shutting down, total bytes read from upstream: %llu\n", rb->bytes_read);

	return 0;
}
