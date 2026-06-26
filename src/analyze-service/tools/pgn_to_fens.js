#!/usr/bin/env node

const fs = require("fs");
const { Chess } = require("chess.js");

const pgnFile = process.argv[2];
if (!pgnFile) {
  console.error("Usage: pgn_to_fens.js <game.pgn> [moves-out.json]");
  process.exit(1);
}

const pgn = fs.readFileSync(pgnFile, "utf8");
const chess = new Chess();
try {
  chess.loadPgn(pgn);
} catch (e) {
  console.error("Failed to load PGN:", e.message);
  process.exit(1);
}

const history = chess.history({ verbose: true });
const replay = new Chess();

const fens = [replay.fen()];
const sanList = [];
const captureList = [];

for (const move of history) {
  replay.move(move);
  fens.push(replay.fen());
  sanList.push(move.san);
  captureList.push(move.captured ? 1 : 0);
}

for (let i = 0; i < fens.length; i++) {
  process.stdout.write(fens[i] + "\n");
}

const jsonFile = process.argv[3];
if (jsonFile) {
  const data = { sans: sanList, captures: captureList };
  fs.writeFileSync(jsonFile, JSON.stringify(data, null, 2));
}
