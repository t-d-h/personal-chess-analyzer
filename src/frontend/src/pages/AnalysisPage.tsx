import { useEffect, useState } from "react";
import { useParams } from "react-router-dom";
import { getAnalysis, getGameMeta, type AnalysisResponse, type GameMeta } from "../services/api";
import { useJobPoller } from "../hooks/useJobPoller";
import { ProgressBar } from "../components/ProgressBar";
import { ChessboardView } from "../components/Chessboard";
import { MoveList } from "../components/MoveList";
import { EvalGraph } from "../components/EvalGraph";
import { PlayerStats } from "../components/PlayerStats";

export function AnalysisPage() {
  const { gameId } = useParams<{ gameId: string }>();
  const [meta, setMeta] = useState<GameMeta | null>(null);
  const [analysis, setAnalysis] = useState<AnalysisResponse | null>(null);
  const [jobId, setJobId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [currentPly, setCurrentPly] = useState(1);

  const jobStatus = useJobPoller(jobId);

  useEffect(() => {
    if (!gameId) return;

    getGameMeta(gameId)
      .then((m) => {
        setMeta(m);
        if (m.status === "completed") {
          return getAnalysis(gameId).then((a) => setAnalysis(a));
        } else {
          setJobId(gameId);
        }
      })
      .catch((err) => setError(err.message));
  }, [gameId]);

  useEffect(() => {
    if (jobStatus?.status === "completed" && gameId) {
      getAnalysis(gameId)
        .then((a) => setAnalysis(a))
        .catch((err) => setError(err.message));
    }
    if (jobStatus?.status === "failed") {
      setError(jobStatus.error ?? "Analysis failed");
    }
  }, [jobStatus?.status, gameId]);

  if (error) {
    return (
      <div className="analysis-page">
        <p className="error-msg">{error}</p>
      </div>
    );
  }

  if (!meta) {
    return (
      <div className="analysis-page">
        <p>Loading...</p>
      </div>
    );
  }

  const done = meta.status === "completed" || jobStatus?.status === "completed";

  return (
    <div className="analysis-page">
      <header className="game-header">
        <h2>
          {meta.white.username} {meta.white.rating != null ? `(${meta.white.rating})` : ""} vs{" "}
          {meta.black.username} {meta.black.rating != null ? `(${meta.black.rating})` : ""}
        </h2>
        <p className="game-meta">
          Result: {meta.result}
          {meta.timeControl && ` | Time: ${meta.timeControl}`}
          {meta.ecoCode && ` | ECO: ${meta.ecoCode}`}
        </p>
      </header>

      {!done && jobStatus && <ProgressBar job={jobStatus} />}

      {done && analysis && (
        <>
          <div className="analysis-layout">
            <ChessboardView moves={analysis.moves} currentPly={currentPly} onPlyChange={setCurrentPly} />
            <MoveList moves={analysis.moves} currentPly={currentPly} onPlyChange={setCurrentPly} />
          </div>
          <EvalGraph moves={analysis.moves} />
          <PlayerStats summaries={analysis.playerSummaries} />
        </>
      )}
    </div>
  );
}
