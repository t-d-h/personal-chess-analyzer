import React, { useState } from 'react';

interface InputFormProps {
  onSubmit: (data: { pgn?: string; url?: string }) => void;
  isLoading: boolean;
}

export const InputForm: React.FC<InputFormProps> = ({ onSubmit, isLoading }) => {
  const [pgn, setPgn] = useState('');
  const [url, setUrl] = useState('');
  const [error, setError] = useState<string | null>(null);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);

    const trimmedPgn = pgn.trim();
    const trimmedUrl = url.trim();

    if (!trimmedPgn && !trimmedUrl) {
      setError('Please provide a PGN or a Chess.com game URL.');
      return;
    }

    if (trimmedPgn && trimmedUrl) {
      setError('Please provide only one: either a PGN or a Chess.com game URL.');
      return;
    }

    if (trimmedUrl) {
      if (!trimmedUrl.includes('chess.com/game/') && !trimmedUrl.includes('chess.com/analysis/game/')) {
        setError('URL must be a valid Chess.com game URL (containing chess.com/game/ or chess.com/analysis/game/).');
        return;
      }
      onSubmit({ url: trimmedUrl });
    } else {
      // If the pasted PGN itself looks like a Chess.com URL, treat it as a URL
      if (trimmedPgn.includes('chess.com/game/') || trimmedPgn.includes('chess.com/analysis/game/')) {
        onSubmit({ url: trimmedPgn });
      } else {
        onSubmit({ pgn: trimmedPgn });
      }
    }
  };

  return (
    <form id="analysis-form" onSubmit={handleSubmit} className="input-form">
      <h2>Analyze a Chess Game</h2>
      <p className="subtitle">
        Enter a raw PGN or a Chess.com game URL to run a Stockfish evaluation and get move classifications.
      </p>

      <div className="input-group">
        <label htmlFor="pgn-input">Paste PGN</label>
        <textarea
          id="pgn-input"
          placeholder="[Event &quot;Casual Game&quot;]&#10;1. e4 e5 2. Nf3 Nc6..."
          value={pgn}
          onChange={(e) => {
            setPgn(e.target.value);
            if (e.target.value.trim()) setUrl(''); // Clear the other input
          }}
          disabled={isLoading}
          rows={8}
        />
      </div>

      <div className="or-divider">
        <span>OR</span>
      </div>

      <div className="input-group">
        <label htmlFor="url-input">Chess.com Game URL</label>
        <input
          type="text"
          id="url-input"
          placeholder="https://www.chess.com/game/live/..."
          value={url}
          onChange={(e) => {
            setUrl(e.target.value);
            if (e.target.value.trim()) setPgn(''); // Clear the other input
          }}
          disabled={isLoading}
        />
      </div>

      {error && <div className="form-error" id="form-error">{error}</div>}

      <button
        type="submit"
        id="submit-button"
        className="btn btn-primary btn-block"
        disabled={isLoading}
      >
        {isLoading ? 'Submitting...' : 'Start Analysis'}
      </button>
    </form>
  );
};
