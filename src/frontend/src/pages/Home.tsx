import { useNavigate } from "react-router-dom";
import { InputForm } from "../components/InputForm";

export function Home() {
  const navigate = useNavigate();

  const handleSubmit = (gameId: string, _jobId: string) => {
    navigate(`/analysis/${gameId}`);
  };

  return (
    <div className="home-page">
      <h1 className="title">&#9817; Chess Analyzer</h1>
      <InputForm onSubmit={handleSubmit} />
    </div>
  );
}
