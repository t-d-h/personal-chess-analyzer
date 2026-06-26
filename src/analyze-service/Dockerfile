FROM ubuntu:24.04
RUN apt-get update && apt-get install -y stockfish gcc make libhiredis-dev libmongoc-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make build
CMD ["./bin/analyze-worker"]
