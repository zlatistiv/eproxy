#!/bin/bash                    
# We should be able to easily handle thousands of connections
# Run this script and watch out for any anomalies in the video stream
# In a seperate session run: ffplay tcp://<host>:8080
                                               
ulimit -n 65535                      
N_CLIENTS=1024
                                               
cleanup() {                                                                                    
        kill -SIGINT "$PIPE_PID" 2>/dev/null                                                   
        wait "$PIPE_PID" 2>/dev/null                                                           
        exit                                                                                   
}                                                                                              
                                               
trap cleanup INT TERM EXIT     
                                               
ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=30 \
        -f lavfi -i sine=frequency=440:sample_rate=44100 \
        -c:v libx264 -preset ultrafast -tune zerolatency -c:a aac \
        -f mpegts - 2>/dev/null | ./eproxy -maxconn $(($N_CLIENTS + 100)) &

PIPE_PID=$!
sleep 5

while true; do
        pids=()

        for i in $(seq 1 "$N_CLIENTS"); do
                nc localhost 8080 >/dev/null 2>&1 &
                pids+=($!)
        done

        sleep 60
        kill "${pids[@]}" 2>/dev/null

        sleep 1
done
