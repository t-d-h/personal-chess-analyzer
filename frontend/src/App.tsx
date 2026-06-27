import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { Home } from './pages/Home';
import { AnalysisPage } from './pages/AnalysisPage';
import './index.css';

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Home />} />
        <Route path="/analysis/:gameId" element={<AnalysisPage />} />
      </Routes>
    </BrowserRouter>
  );
}

export default App;
