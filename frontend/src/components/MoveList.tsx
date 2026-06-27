import React from 'react';
import type { AnalysisMove } from '../services/api';

interface MoveListProps {
  moves: AnalysisMove[];
  currentPly: number;
  onSelectPly: (ply: number) => void;
}

export const MoveList: React.FC<MoveListProps> = ({ moves, currentPly, onSelectPly }) => {
  const getBadgeSymbol = (classification: string) => {
    switch (classification) {
      case 'brilliant': return '!!';
      case 'best': return '★';
      case 'excellent': return '◎';
      case 'good': return '✓';
      case 'book': return '📖';
      case 'inaccuracy': return '┲';
      case 'mistake': return '?';
      case 'blunder': return '??';
      default: return '';
    }
  };

  // Group moves into pairs (White / Black)
  const rows: { moveNumber: number; white?: AnalysisMove; black?: AnalysisMove }[] = [];
  
  for (let i = 0; i < moves.length; i += 2) {
    const white = moves[i];
    const black = moves[i + 1];
    rows.push({
      moveNumber: Math.floor(i / 2) + 1,
      white,
      black
    });
  }

  return (
    <div className="movelist-container" id="movelist-container">
      <h3>Move List</h3>
      <div className="movelist-scroll">
        <div
          className={`move-row start-row ${currentPly === 0 ? 'active' : ''}`}
          onClick={() => onSelectPly(0)}
        >
          <span className="move-num">0.</span>
          <span className="move-san-start">Starting Position</span>
        </div>
        {rows.map((row) => (
          <div key={row.moveNumber} className="move-row">
            <span className="move-num">{row.moveNumber}.</span>
            
            {row.white && (
              <span
                className={`move-item move-white ${currentPly === row.white.ply ? 'active' : ''}`}
                onClick={() => onSelectPly(row.white!.ply)}
                id={`move-ply-${row.white.ply}`}
              >
                <span className="san">{row.white.san}</span>
                {row.white.classification && (
                  <span className={`badge badge-${row.white.classification}`}>
                    {getBadgeSymbol(row.white.classification)} {row.white.classification}
                  </span>
                )}
              </span>
            )}

            {row.black && (
              <span
                className={`move-item move-black ${currentPly === row.black.ply ? 'active' : ''}`}
                onClick={() => onSelectPly(row.black!.ply)}
                id={`move-ply-${row.black.ply}`}
              >
                <span className="san">{row.black.san}</span>
                {row.black.classification && (
                  <span className={`badge badge-${row.black.classification}`}>
                    {getBadgeSymbol(row.black.classification)} {row.black.classification}
                  </span>
                )}
              </span>
            )}
          </div>
        ))}
      </div>
    </div>
  );
};
