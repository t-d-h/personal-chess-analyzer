import React from 'react';
import { Chessboard as ReactChessboard } from 'react-chessboard';

interface ChessboardProps {
  fen: string;
  orientation: 'white' | 'black';
  onFlip: () => void;
  onPrev: () => void;
  onNext: () => void;
  currentPly: number;
  totalPlies: number;
}

export const Chessboard: React.FC<ChessboardProps> = ({
  fen,
  orientation,
  onFlip,
  onPrev,
  onNext,
  currentPly,
  totalPlies
}) => {
  return (
    <div className="chessboard-container" id="chessboard-container">
      <div className="chessboard-wrapper">
        <ReactChessboard
          key={fen}
          options={{
            position: fen,
            boardOrientation: orientation,
            allowDragging: false,
            boardStyle: {
              borderRadius: '8px',
              boxShadow: '0 8px 24px rgba(0, 0, 0, 0.3)',
              width: '400px',
              height: '400px'
            }
          }}
        />
      </div>
      <div className="chessboard-controls">
        <button
          id="prev-move-btn"
          className="btn btn-secondary"
          onClick={onPrev}
          disabled={currentPly <= 0}
          title="Previous Move"
        >
          ←
        </button>
        <span className="move-counter" id="move-counter">
          Ply {currentPly} / {totalPlies}
        </span>
        <button
          id="next-move-btn"
          className="btn btn-secondary"
          onClick={onNext}
          disabled={currentPly >= totalPlies}
          title="Next Move"
        >
          →
        </button>
        <button
          id="flip-board-btn"
          className="btn btn-outline"
          onClick={onFlip}
          title="Flip Board"
          style={{ marginLeft: '12px' }}
        >
          Flip
        </button>
      </div>
    </div>
  );
};
