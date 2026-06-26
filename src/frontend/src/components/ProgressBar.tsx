import type { JobStatus } from "../services/api";

interface Props {
  job: JobStatus;
}

export function ProgressBar({ job }: Props) {
  const total = job.movesTotal || 1;
  const pct = Math.round((job.movesAnalyzed / total) * 100);

  return (
    <div className="progress-bar-container">
      <p className="progress-text">
        Analyzing... move {job.movesAnalyzed} of {job.movesTotal}
      </p>
      <div className="progress-track">
        <div className="progress-fill" style={{ width: `${pct}%` }} />
      </div>
      <p className="progress-pct">{pct}%</p>
      {job.error && <p className="error-msg">{job.error}</p>}
    </div>
  );
}
