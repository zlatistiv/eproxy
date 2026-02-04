# eproxy

Eproxy serves an upsteam data source (stdin or tcp)
to multiple clients over tcp.

It uses the Linux epoll event loop to distribute
data efficiently across multiple clients.

View the "--help" output for details on command line args.

One example use case is to stream the output of ffmpeg to multiple clients:

```
ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
        -f lavfi -i sine=frequency=440:sample_rate=44100 \
        -c:v libx264 -preset ultrafast -tune zerolatency -c:a aac \
        -f mpegts - 2>/dev/null | ./eproxy
```
