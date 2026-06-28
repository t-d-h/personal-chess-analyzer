import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { InputForm } from '../components/InputForm';
import { PlayerScanner } from '../components/PlayerScanner';
import { postGame } from '../services/api';

export const Home: React.FC = () => {
  const navigate = useNavigate();
  const [activeTab, setActiveTab] = useState<'analyze' | 'scan'>('analyze');
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleSubmit = async (data: { pgn?: string; url?: string }) => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await postGame(data as { pgn: string } | { url: string });
      if (response.chesscomGameId && response.gameType) {
        navigate(`/game/${response.gameType}/${response.chesscomGameId}`);
      } else {
        navigate(`/analysis/${response.gameId}`);
      }
    } catch (err: any) {
      setError(err.message || 'An error occurred while submitting the game.');
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <div className="home-page-container">
      <header className="home-header">
        <h1>♟ Antigravity Chess Analyzer</h1>
      </header>
      <main className="home-main">
        <div className="tabs-container">
          <button
            id="tab-analyze"
            className={`tab-btn ${activeTab === 'analyze' ? 'active' : ''}`}
            onClick={() => setActiveTab('analyze')}
          >
            Analyze Game
          </button>
          <button
            id="tab-scan"
            className={`tab-btn ${activeTab === 'scan' ? 'active' : ''}`}
            onClick={() => setActiveTab('scan')}
          >
            Scan Chess.com User
          </button>
        </div>

        <div className="card glass-card">
          {activeTab === 'analyze' ? (
            <InputForm onSubmit={handleSubmit} isLoading={isLoading} />
          ) : (
            <PlayerScanner onAnalyze={(url) => handleSubmit({ url })} isAnalyzing={isLoading} />
          )}
          {error && (
            <div className="error-banner" id="home-error-banner">
              <strong>Error:</strong> {error}
            </div>
          )}
        </div>
      </main>
    </div>
  );
};
