FROM ubuntu:24.04
RUN apt-get update && apt-get install -y stockfish gcc make pkg-config libhiredis-dev libmongoc-dev nodejs npm
WORKDIR /app
COPY . .
RUN npm init -y && npm install chess.js@1.0.0-beta.6
RUN make build-worker
CMD ["./bin/analyze-worker"]
