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
        <div className="rating-box" style={{ textAlign: 'center', marginBottom: '0.5rem' }}>
          <span className="rating-val" style={{ fontSize: '1.25rem', fontWeight: 'bold', color: 'var(--accent)' }}>{summary.estimatedRating}</span>
          <span className="rating-label" style={{ display: 'block', fontSize: '0.8rem', color: 'var(--text-muted)' }}>Est. Rating</span>
        </div>
        <div className="acpl-box">
          <span className="acpl-label">ACPL: </span>
          <span className="acpl-val">{summary.acpl}</span>
        </div>
        <div className="phase-review-box" style={{ margin: '1rem 0', padding: '0.5rem', background: 'var(--bg-secondary)', borderRadius: '8px' }}>
          <h5 style={{ margin: '0 0 0.5rem 0', textAlign: 'center', color: 'var(--text-primary)' }}>Phase Review</h5>
          <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.9rem' }}>
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontWeight: 'bold' }}>{summary.openingAccuracy.toFixed(1)}%</div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>Opening</div>
            </div>
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontWeight: 'bold' }}>{summary.midgameAccuracy.toFixed(1)}%</div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>Midgame</div>
            </div>
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontWeight: 'bold' }}>{summary.endgameAccuracy.toFixed(1)}%</div>
              <div style={{ fontSize: '0.75rem', color: 'var(--text-muted)' }}>Endgame</div>
            </div>
          </div>
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
