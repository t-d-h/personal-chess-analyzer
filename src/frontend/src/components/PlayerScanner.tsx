import React, { useState } from 'react';
import { getPlayerGames } from '../services/api';
import type { ChesscomGame } from '../services/api';

interface PlayerScannerProps {
  onAnalyze: (url: string) => Promise<void>;
  isAnalyzing: boolean;
}

export const PlayerScanner: React.FC<PlayerScannerProps> = ({ onAnalyze, isAnalyzing }) => {
  const [username, setUsername] = useState('');
  const [lastScannedUsername, setLastScannedUsername] = useState('');
  const [games, setGames] = useState<ChesscomGame[]>([]);
  const [hasMore, setHasMore] = useState(false);
  const [page, setPage] = useState(1);
  const [isLoadingGames, setIsLoadingGames] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchGamesForPage = async (targetPage: number, targetUser = lastScannedUsername) => {
    setIsLoadingGames(true);
    setError(null);
    try {
      const data = await getPlayerGames(targetUser, targetPage, 10);
      setGames(data.games);
      setHasMore(data.hasMore);
    } catch (err: any) {
      setError(err.message || 'Failed to scan player games.');
      setGames([]);
      setHasMore(false);
    } finally {
      setIsLoadingGames(false);
    }
  };

  const handleScanSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    const trimmed = username.trim();
    if (!trimmed) {
      setError('Please enter a username.');
      return;
    }
    setPage(1);
    setGames([]);
    setHasMore(false);
    setLastScannedUsername(trimmed);
    await fetchGamesForPage(1, trimmed);
  };

  const handlePrevPage = async () => {
    if (page > 1) {
      const nextPage = page - 1;
      await fetchGamesForPage(nextPage);
      setPage(nextPage);
      const listEl = document.getElementById('scanner-games-list');
      if (listEl) listEl.scrollTop = 0;
    }
  };

  const handleNextPage = async () => {
    if (hasMore) {
      const nextPage = page + 1;
      await fetchGamesForPage(nextPage);
      setPage(nextPage);
      const listEl = document.getElementById('scanner-games-list');
      if (listEl) listEl.scrollTop = 0;
    }
  };

  return (
    <div className="scanner-container">
      <h2>Scan Chess.com User</h2>
      <p className="subtitle">
        Enter a Chess.com username to scan their game archives and select a game to analyze.
      </p>

      <form onSubmit={handleScanSubmit} className="input-form">
        <div className="input-group" style={{ flexDirection: 'row', gap: '12px', alignItems: 'center' }}>
          <input
            type="text"
            id="scanner-username-input"
            placeholder="Chess.com Username"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            disabled={isLoadingGames || isAnalyzing}
            style={{ flex: 1, marginBottom: 0 }}
          />
          <button
            type="submit"
            id="scanner-scan-btn"
            className="btn btn-primary"
            disabled={isLoadingGames || isAnalyzing || !username.trim()}
            style={{ padding: '12px 24px', height: '48px' }}
          >
            {isLoadingGames ? 'Scanning...' : 'Scan'}
          </button>
        </div>
      </form>

      {error && (
        <div className="error-banner" id="scanner-error-banner">
          <strong>Error:</strong> {error}
        </div>
      )}

      {isLoadingGames && games.length === 0 && (
        <div id="scanner-loading-indicator" className="loading-container" style={{ textAlign: 'center' }}>
          <p>Loading games from Chess.com archives...</p>
        </div>
      )}

      {games.length > 0 && (
        <>
          <div id="scanner-games-list" className="scanner-games-list">
            {games.map((game) => {
              const dateStr = game.playedAt ? new Date(game.playedAt).toLocaleDateString() : 'Unknown date';
              const timeControlStr = game.timeControl || 'Unknown tc';
              const timeClassStr = game.timeClass ? `(${game.timeClass})` : '';

              const whiteName = game.white?.username || 'Unknown';
              const whiteRating = game.white?.rating || 0;
              const blackName = game.black?.username || 'Unknown';
              const blackRating = game.black?.rating || 0;

              const isWhite = whiteName.toLowerCase() === lastScannedUsername.toLowerCase();
              const myResult = isWhite ? game.white?.result : game.black?.result;
              const oppResult = isWhite ? game.black?.result : game.white?.result;

              let outcomeText = 'Draw';
              let outcomeClass = 'outcome-draw';
              if (myResult === 'win') {
                outcomeText = 'Won';
                outcomeClass = 'outcome-win';
              } else if (oppResult === 'win') {
                outcomeText = 'Lost';
                outcomeClass = 'outcome-loss';
              }

              return (
                <div key={game.uuid || game.url} className="scanner-game-card">
                  <div className="scanner-game-info">
                    <div className="scanner-game-players">
                      <span className={`scanner-player ${isWhite && myResult === 'win' ? 'win' : ''}`}>
                        ⚪ {whiteName} ({whiteRating})
                      </span>
                      <span className="vs">vs</span>
                      <span className={`scanner-player ${!isWhite && myResult === 'win' ? 'win' : ''}`}>
                        ⚫ {blackName} ({blackRating})
                      </span>
                    </div>
                    <div className="scanner-game-meta">
                      <span>📅 {dateStr}</span>
                      <span>⏱️ {timeControlStr} {timeClassStr}</span>
                      <span>🔧 {game.rules}</span>
                    </div>
                  </div>
                  <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
                    <span className={`scanner-outcome ${outcomeClass}`}>{outcomeText}</span>
                    <button
                      className="btn btn-secondary btn-sm scanner-analyze-btn"
                      onClick={() => onAnalyze(game.url)}
                      disabled={isAnalyzing || isLoadingGames}
                    >
                      {isAnalyzing ? 'Analyzing...' : 'Analyze'}
                    </button>
                  </div>
                </div>
              );
            })}
          </div>

          <div className="pagination-controls">
            <button
              id="scanner-prev-btn"
              className="btn btn-secondary btn-sm"
              onClick={handlePrevPage}
              disabled={page === 1 || isLoadingGames || isAnalyzing}
            >
              Previous
            </button>
            <span id="scanner-page-info" className="pagination-page-info">
              Page {page}
            </span>
            <button
              id="scanner-next-btn"
              className="btn btn-secondary btn-sm"
              onClick={handleNextPage}
              disabled={!hasMore || isLoadingGames || isAnalyzing}
            >
              Next
            </button>
          </div>
        </>
      )}
    </div>
  );
};
