import React, { useEffect, useRef } from "react";
import type { Classification, MoveData } from "../services/api";

const CLASSIFICATION_BADGE: Record<Classification, { symbol: string; className: string }> = {
  brilliant: { symbol: "\u2726", className: "badge-brilliant" },
  best: { symbol: "\u2605", className: "badge-best" },
  excellent: { symbol: "\u25CE", className: "badge-excellent" },
  good: { symbol: "\u25CF", className: "badge-good" },
  book: { symbol: "\u265B", className: "badge-book" },
  inaccuracy: { symbol: "\u25B3", className: "badge-inaccuracy" },
  mistake: { symbol: "\u25B4", className: "badge-mistake" },
  blunder: { symbol: "\u2718", className: "badge-blunder" },
};

interface Props {
  moves: MoveData[];
  currentPly: number;
  onPlyChange: (ply: number) => void;
}

export function MoveList({ moves, currentPly, onPlyChange }: Props) {
  const activeRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    activeRef.current?.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }, [currentPly]);

  const grouped: { moveNum: number; white: MoveData | null; black: MoveData | null }[] = [];
  for (const move of moves) {
    const moveNum = Math.floor((move.ply - 1) / 2) + 1;
    if (move.color === "white") {
      grouped.push({ moveNum, white: move, black: null });
    } else if (grouped.length > 0) {
      grouped[grouped.length - 1].black = move;
    }
  }

  return (
    <div className="move-list">
      {grouped.map((g) => (
        <div key={g.moveNum} className="move-row">
          <span className="move-num">{g.moveNum}.</span>
          {g.white && (
            <MoveEntry move={g.white} active={g.white.ply === currentPly} onClick={onPlyChange} ref={g.white.ply === currentPly ? activeRef : undefined} />
          )}
          {g.black && (
            <MoveEntry move={g.black} active={g.black.ply === currentPly} onClick={onPlyChange} ref={g.black.ply === currentPly ? activeRef : undefined} />
          )}
        </div>
      ))}
    </div>
  );
}

interface MoveEntryProps {
  move: MoveData;
  active: boolean;
  onClick: (ply: number) => void;
}

const MoveEntry = React.forwardRef<HTMLDivElement, MoveEntryProps>(({ move, active, onClick }, ref) => {
  const badge = move.classification ? CLASSIFICATION_BADGE[move.classification] : null;

  return (
    <div
      ref={ref}
      className={`move-entry ${active ? "move-active" : ""}`}
      onClick={() => onClick(move.ply)}
    >
      <span className="move-san">{move.san}</span>
      {badge && (
        <span className={`badge ${badge.className}`} title={move.classification ?? undefined}>
          {badge.symbol}
        </span>
      )}
    </div>
  );
});
