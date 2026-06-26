import { useCallback } from "react";
import { Chess } from "chess.js";
import { Chessboard } from "react-chessboard";
import type { MoveData } from "../services/api";

interface Props {
  moves: MoveData[];
  currentPly: number;
  onPlyChange: (ply: number) => void;
}

export function ChessboardView({ moves, currentPly, onPlyChange }: Props) {
  const getPosition = useCallback(() => {
    const move = moves.find((m) => m.ply === currentPly);
    if (!move) return "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const chess = new Chess(move.fenBefore);
    return chess.fen();
  }, [moves, currentPly]);

  const canPrev = currentPly > 0;
  const canNext = currentPly < moves.length;

  return (
    <div className="chessboard-container">
      <div className="chessboard-wrapper">
        <Chessboard position={getPosition()} areArrowsAllowed={false} arePiecesDraggable={false} />
      </div>
      <div className="chessboard-nav">
        <button onClick={() => onPlyChange(0)} disabled={!canPrev} aria-label="First move">
          ⏮
        </button>
        <button onClick={() => onPlyChange(currentPly - 1)} disabled={!canPrev} aria-label="Previous move">
          ←
        </button>
        <span className="ply-label">
          Move {currentPly}/{moves.length}
        </span>
        <button onClick={() => onPlyChange(currentPly + 1)} disabled={!canNext} aria-label="Next move">
          →
        </button>
        <button onClick={() => onPlyChange(moves.length)} disabled={!canNext} aria-label="Last move">
          ⏭
        </button>
      </div>
    </div>
  );
}
