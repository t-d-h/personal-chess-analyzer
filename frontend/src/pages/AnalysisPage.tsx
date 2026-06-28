import React, { useEffect, useState } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { getGameMeta, getAnalysis } from '../services/api';
import type { GameMeta, AnalysisResponse } from '../services/api';
import { useJobPoller } from '../hooks/useJobPoller';
import { Chessboard } from '../components/Chessboard';
import { MoveList } from '../components/MoveList';
import { EvalGraph } from '../components/EvalGraph';
import { PlayerStats } from '../components/PlayerStats';
import { ProgressBar } from '../components/ProgressBar';

export const AnalysisPage: React.FC = () => {
  const { gameId } = useParams<{ gameId: string }>();
  const navigate = useNavigate();

  const [meta, setMeta] = useState<GameMeta | null>(null);
  const [analysis, setAnalysis] = useState<AnalysisResponse | null>(null);
  const [currentPly, setCurrentPly] = useState(0);
  const [orientation, setOrientation] = useState<'white' | 'black'>('white');
  
  const [metaError, setMetaError] = useState<string | null>(null);
  const [analysisError, setAnalysisError] = useState<string | null>(null);

  // Hook for polling progress
  const { jobState, error: pollerError } = useJobPoller(gameId || null);

  // 1. Fetch metadata on load
  useEffect(() => {
    if (!gameId) return;
    
    getGameMeta(gameId)
      .then((data) => {
        setMeta(data);
        // Set board orientation from black's perspective if white is a bot or player choice
        // defaulting to white is standard
      })
      .catch((err) => {
        setMetaError(err.message || 'Failed to load game metadata');
      });
  }, [gameId]);

  // 2. Fetch analysis once completed
  useEffect(() => {
    if (!gameId) return;

    if (meta?.status === 'completed' || jobState?.status === 'completed') {
      getAnalysis(gameId)
        .then((data) => {
          setAnalysis(data);
          // Set currentPly to the end of the game by default
          setCurrentPly(data.moves.length);
        })
        .catch((err) => {
          setAnalysisError(err.message || 'Failed to load analysis details');
        });
    }
  }, [gameId, meta?.status, jobState?.status]);

  // 3. Arrow key navigation
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (!analysis) return;
      if (e.key === 'ArrowLeft') {
        setCurrentPly((prev) => Math.max(0, prev - 1));
      } else if (e.key === 'ArrowRight') {
        setCurrentPly((prev) => Math.min(analysis.moves.length, prev + 1));
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [analysis]);

  if (metaError) {
    return (
      <div className="status-page error-page">
        <div className="card glass-card">
          <h2>Error Loading Game</h2>
          <p>{metaError}</p>
          <button className="btn btn-primary" onClick={() => navigate('/')}>Go Home</button>
        </div>
      </div>
    );
  }

  if ((pollerError || jobState?.status === 'failed') && meta?.status !== 'completed') {
    return (
      <div className="status-page error-page">
        <div className="card glass-card">
          <h2>Analysis Failed</h2>
          <p>{pollerError || jobState?.error || 'Unknown error occurred during analysis.'}</p>
          <button className="btn btn-primary" onClick={() => navigate('/')}>Go Home</button>
        </div>
      </div>
    );
  }

  // Determine current FEN position
  const startFen = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
  const currentFen =
    currentPly === 0 || !analysis
      ? startFen
      : analysis.moves[currentPly - 1]?.fenAfter || startFen;

  console.log('currentPly:', currentPly, 'currentFen:', currentFen);

  const totalPlies = analysis ? analysis.moves.length : 0;

  const handlePrev = () => setCurrentPly((prev) => Math.max(0, prev - 1));
  const handleNext = () => setCurrentPly((prev) => Math.min(totalPlies, prev + 1));
  const handleFlip = () => setOrientation((prev) => (prev === 'white' ? 'black' : 'white'));
  const handleSelectPly = (ply: number) => setCurrentPly(ply);


  return (
    <div className="analysis-page-container">
      <header className="analysis-header">
        <button className="btn btn-secondary btn-sm" onClick={() => navigate('/')}>
          ← New Analysis
        </button>
        <h1>♟ Game Analysis</h1>
      </header>

      {meta && (
        <div className="game-meta-banner" id="game-meta-banner">
          <div className="players-info">
            <span className="player white-player">
              ⚪ {meta.white.username} ({meta.white.rating})
            </span>
            <span className="vs">vs</span>
            <span className="player black-player">
              ⚫ {meta.black.username} ({meta.black.rating})
            </span>
          </div>
          <div className="meta-details">
            <span><strong>Result:</strong> {meta.result}</span>
            <span><strong>Time Control:</strong> {meta.timeControl}</span>
            <span><strong>ECO:</strong> {meta.ecoCode}</span>
            {meta.playedAt && (
              <span><strong>Played:</strong> {new Date(meta.playedAt).toLocaleDateString()}</span>
            )}
          </div>
        </div>
      )}

      {/* Show progress bar if not completed */}
      {(!jobState || jobState.status === 'queued' || jobState.status === 'running') && (
        <div className="loading-container card glass-card">
          <ProgressBar
            status={jobState?.status || 'queued'}
            movesAnalyzed={jobState?.movesAnalyzed || 0}
            movesTotal={jobState?.movesTotal || (meta ? 0 : 0)}
          />
        </div>
      )}

      {analysisError && (
        <div className="error-banner card">
          <strong>Error loading analysis details:</strong> {analysisError}
        </div>
      )}

      {analysis && (
        <div className="analysis-grid-layout" id="analysis-grid-layout">
          <div className="left-panel">
            <Chessboard
              fen={currentFen}
              orientation={orientation}
              onFlip={handleFlip}
              onPrev={handlePrev}
              onNext={handleNext}
              currentPly={currentPly}
              totalPlies={totalPlies}
            />
          </div>

          <div className="right-panel">
            <MoveList
              moves={analysis.moves}
              currentPly={currentPly}
              onSelectPly={handleSelectPly}
            />
          </div>

          <div className="bottom-panel full-width">
            <EvalGraph
              moves={analysis.moves}
              currentPly={currentPly}
              onSelectPly={handleSelectPly}
            />
          </div>

          <div className="stats-panel full-width">
            <PlayerStats
              playerSummaries={analysis.playerSummaries}
              whiteName={meta?.white.username || 'White'}
              blackName={meta?.black.username || 'Black'}
            />
          </div>
        </div>
      )}
    </div>
  );
};
