import React from 'react';
import type { PlayerSummary } from '../services/api';

interface PlayerStatsProps {
  playerSummaries: PlayerSummary[];
  whiteName: string;
  blackName: string;
}

export const PlayerStats: React.FC<PlayerStatsProps> = ({
  playerSummaries,
  whiteName,
  blackName
}) => {
  const whiteSummary = playerSummaries.find((s) => s.color === 'white');
  const blackSummary = playerSummaries.find((s) => s.color === 'black');

  const renderPlayerColumn = (
    summary: PlayerSummary | undefined,
    playerName: string,
    colorClass: string
  ) => {
    if (!summary) return <div className="player-stats-col">No data for {playerName}</div>;

    const statsList = [
      { label: 'Brilliant', count: summary.brilliantCount, class: 'brilliant' },
      { label: 'Best', count: summary.bestCount, class: 'best' },
      { label: 'Excellent', count: summary.excellentCount, class: 'excellent' },
      { label: 'Good', count: summary.goodCount, class: 'good' },
      { label: 'Inaccuracy', count: summary.inaccuracyCount, class: 'inaccuracy' },
      { label: 'Mistake', count: summary.mistakeCount, class: 'mistake' },
      { label: 'Blunder', count: summary.blunderCount, class: 'blunder' }
    ];

    return (
      <div className={`player-stats-col ${colorClass}`}>
        <h4>{playerName}</h4>
        <div className="accuracy-box">
          <span className="accuracy-val">{summary.accuracyPct.toFixed(1)}%</span>
          <span className="accuracy-label">Accuracy</span>
        </div>
        <div className="acpl-box">
          <span className="acpl-label">ACPL: </span>
          <span className="acpl-val">{summary.acpl}</span>
        </div>
        <div className="classifications-grid">
          {statsList.map((stat) => (
            <div key={stat.label} className="stat-card">
              <span className={`badge badge-${stat.class}`}>{stat.label}</span>
              <span className="stat-count">{stat.count}</span>
            </div>
          ))}
        </div>
      </div>
    );
  };

  return (
    <div className="player-stats-container" id="player-stats-container">
      <h3>Player Performance Stats</h3>
      <div className="player-stats-columns">
        {renderPlayerColumn(whiteSummary, whiteName || 'White Player', 'white-player-col')}
        {renderPlayerColumn(blackSummary, blackName || 'Black Player', 'black-player-col')}
      </div>
    </div>
  );
};
