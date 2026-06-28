import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { InputForm } from '../components/InputForm';
import { postGame } from '../services/api';

export const Home: React.FC = () => {
  const navigate = useNavigate();
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleSubmit = async (data: { pgn?: string; url?: string }) => {
    setIsLoading(true);
    setError(null);
    try {
      const response = await postGame(data as { pgn: string } | { url: string });
      // Navigate to the analysis page, passing the gameId (which is also the jobId)
      navigate(`/analysis/${response.gameId}`);
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
        <div className="card glass-card">
          <InputForm onSubmit={handleSubmit} isLoading={isLoading} />
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
