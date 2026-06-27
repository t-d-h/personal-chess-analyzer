import React from 'react';

interface ProgressBarProps {
  status: string;
  movesAnalyzed: number;
  movesTotal: number;
}

export const ProgressBar: React.FC<ProgressBarProps> = ({ status, movesAnalyzed, movesTotal }) => {
  const percentage = movesTotal > 0 ? Math.min(Math.round((movesAnalyzed / movesTotal) * 100), 100) : 0;

  return (
    <div className="progress-container" id="progress-container">
      <div className="progress-header">
        <span className="status-badge" id="progress-status">
          {status === 'queued' ? 'Queued' : 'Analyzing'}
        </span>
        <span className="progress-text" id="progress-text">
          {status === 'queued'
            ? 'Waiting for worker...'
            : `Analyzing: move ${movesAnalyzed} of ${movesTotal} (${percentage}%)`}
        </span>
      </div>
      <div className="progress-bar-bg">
        <div
          className="progress-bar-fill"
          id="progress-bar-fill"
          style={{ width: `${percentage}%` }}
        />
      </div>
    </div>
  );
};
