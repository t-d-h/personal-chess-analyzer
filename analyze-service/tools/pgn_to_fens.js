const fs = require('fs');
const path = require('path');
const { Chess } = require(path.join(__dirname, '../../src/api-gateway/node_modules/chess.js'));

function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) {
        console.error('Usage: node pgn_to_fens.js <pgn_file>');
        process.exit(1);
    }

    const pgnFile = args[0];
    let pgnContent;
    try {
        pgnContent = fs.readFileSync(pgnFile, 'utf8');
    } catch (err) {
        console.error(`Failed to read file ${pgnFile}: ${err.message}`);
        process.exit(1);
    }

    const chess = new Chess();
    try {
        chess.loadPgn(pgnContent);
    } catch (err) {
        console.error(`Failed to load PGN: ${err.message}`);
        process.exit(1);
    }

    const moves = chess.history({ verbose: true });
    // Replay the game to output states
    const tracker = new Chess();
    for (let i = 0; i < moves.length; i++) {
        const move = moves[i];
        const ply = i + 1;
        const color = move.color === 'w' ? 'white' : 'black';
        const san = move.san;
        const fenBefore = tracker.fen();
        
        // Generate mapping of legal moves
        const legalMoves = tracker.moves({ verbose: true });
        const mappingParts = legalMoves.map(m => {
            const uci = m.from + m.to + (m.promotion || '');
            return `${uci}:${m.san}`;
        });
        const mappingStr = mappingParts.join(',');

        tracker.move(move);
        const fenAfter = tracker.fen();

        let status = 'normal';
        if (i === moves.length - 1) {
            if (tracker.isCheckmate()) {
                status = 'checkmate';
            } else if (tracker.isDraw()) {
                status = 'draw';
            }
        }

        console.log(`${ply}|${color}|${san}|${fenBefore}|${fenAfter}|${mappingStr}|${status}`);
    }
}

main();
