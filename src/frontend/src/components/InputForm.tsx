import { useState } from "react";
import { postGame } from "../services/api";

const CHESSCOM_REGEX = /chess\.com\/game\//;

interface Props {
  onSubmit: (gameId: string, jobId: string) => void;
}

export function InputForm({ onSubmit }: Props) {
  const [input, setInput] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError(null);

    const trimmed = input.trim();
    if (!trimmed) {
      setError("Please paste a PGN or enter a chess.com URL");
      return;
    }

    const isUrl = CHESSCOM_REGEX.test(trimmed);
    const body = isUrl ? { url: trimmed } : { pgn: trimmed };

    setSubmitting(true);
    try {
      const res = await postGame(body);
      onSubmit(res.gameId, res.jobId);
    } catch (err) {
      setError(err instanceof Error ? err.message : "Submission failed");
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <form className="input-form" onSubmit={handleSubmit}>
      <label htmlFor="pgn-input">Paste PGN or chess.com URL</label>
      <textarea
        id="pgn-input"
        rows={8}
        placeholder="Paste PGN here...  or  https://chess.com/game/live/..."
        value={input}
        onChange={(e) => setInput(e.target.value)}
        disabled={submitting}
      />
      {error && <p className="error-msg">{error}</p>}
      <button type="submit" disabled={submitting}>
        {submitting ? "Submitting..." : "Analyze"}
      </button>
    </form>
  );
}
