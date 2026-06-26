import type { PlayerSummary } from "../services/api";

interface Props {
  summaries: PlayerSummary[];
}

export function PlayerStats({ summaries }: Props) {
  if (summaries.length === 0) return null;

  return (
    <div className="player-stats-container">
      {summaries.map((s) => (
        <div key={s.color} className={`player-stat ${s.color}`}>
          <h4>{s.color === "white" ? "White" : "Black"}</h4>
          <p className="accuracy">
            Accuracy: <strong>{s.accuracyPct.toFixed(1)}%</strong>
          </p>
          <p className="acpl">ACPL: {s.acpl.toFixed(1)}</p>
          <div className="classification-counts">
            {s.brilliantCount > 0 && <span className="badge badge-brilliant">Brilliant {s.brilliantCount}</span>}
            {s.bestCount > 0 && <span className="badge badge-best">Best {s.bestCount}</span>}
            {s.excellentCount > 0 && <span className="badge badge-excellent">Excellent {s.excellentCount}</span>}
            {s.goodCount > 0 && <span className="badge badge-good">Good {s.goodCount}</span>}
            {s.inaccuracyCount > 0 && <span className="badge badge-inaccuracy">Inaccuracy {s.inaccuracyCount}</span>}
            {s.mistakeCount > 0 && <span className="badge badge-mistake">Mistake {s.mistakeCount}</span>}
            {s.blunderCount > 0 && <span className="badge badge-blunder">Blunder {s.blunderCount}</span>}
          </div>
        </div>
      ))}
    </div>
  );
}
