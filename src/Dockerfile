FROM ubuntu:latest
LABEL authors="ritik"

RUN apt-get update && \
    apt-get install -y g++ make cmake libboost-all-dev libasio-dev

# Set the working directory
WORKDIR /app

# Copy the source code
COPY . /app
RUN mkdir "/var/log/erss"
# Build the application
RUN cmake . && make

# Expose the port
EXPOSE 12345

# Use a wrapper script as the entrypoint
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]