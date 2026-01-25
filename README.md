# eproxy

Eproxy serves an upsteam data source (stdin or tcp)
to multiple clients over tcp.

It uses the Linux epoll event loop to distribute
data efficiently across multiple clients.

View the "--help" output for details on command line args.
