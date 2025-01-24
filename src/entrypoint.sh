#!/bin/bash

# Start the proxy server
./HTTP

# Prevent the container from exiting
while true; do
    sleep 10
done