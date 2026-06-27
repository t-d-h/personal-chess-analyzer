import React from 'react';
import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ReferenceLine
} from 'recharts';
import type { AnalysisMove } from '../services/api';

interface EvalGraphProps {
  moves: AnalysisMove[];
  currentPly: number;
  onSelectPly: (ply: number) => void;
}

export const EvalGraph: React.FC<EvalGraphProps> = ({ moves, currentPly, onSelectPly }) => {
  const getWinPercent = (evalCp: number | null, evalMate: number | null, color: 'white' | 'black') => {
    if (evalMate !== null) {
      if (evalMate > 0) return 100;
      if (evalMate < 0) return 0;
      if (evalMate === 0) {
        return color === 'white' ? 100 : 0;
      }
    }
    if (evalCp !== null) {
      let cp = evalCp;
      if (cp > 10000) cp = 10000;
      if (cp < -10000) cp = -10000;
      return 50.0 + 50.0 * (2.0 / (1.0 + Math.exp(-0.00368208 * cp)) - 1.0);
    }
    return 50.0;
  };

  const chartData = [
    {
      ply: 0,
      winPct: 50.0,
      label: 'Start',
      displayEval: '0.00'
    },
    ...moves.map((m) => {
      const winPct = getWinPercent(m.evalCpPlayed, m.evalMate, m.color);
      let displayEval = '0.00';
      if (m.evalMate !== null) {
        displayEval = m.evalMate > 0 ? `#+${m.evalMate}` : `#${m.evalMate}`;
      } else if (m.evalCpPlayed !== null) {
        displayEval = (m.evalCpPlayed / 100).toFixed(2);
        if (m.evalCpPlayed > 0) displayEval = `+${displayEval}`;
      }
      return {
        ply: m.ply,
        winPct: Number(winPct.toFixed(1)),
        label: `${m.ply}. ${m.color === 'white' ? '' : '...'}${m.san}`,
        displayEval
      };
    })
  ];

  const handleChartClick = (state: any) => {
    if (state && state.activeTooltipIndex !== undefined) {
      onSelectPly(state.activeTooltipIndex);
    }
  };

  const CustomTooltip = ({ active, payload }: any) => {
    if (active && payload && payload.length) {
      const data = payload[0].payload;
      return (
        <div className="custom-tooltip">
          <p className="label">{data.label}</p>
          <p className="eval">Eval: {data.displayEval}</p>
          <p className="winpct">White Win%: {data.winPct}%</p>
        </div>
      );
    }
    return null;
  };

  return (
    <div className="eval-graph-container" id="eval-graph-container">
      <h3>Win% Evaluation Graph</h3>
      <div style={{ width: '100%', height: 250 }}>
        <ResponsiveContainer>
          <LineChart
            data={chartData}
            margin={{ top: 10, right: 30, left: 0, bottom: 0 }}
            onClick={handleChartClick}
          >
            <CartesianGrid strokeDasharray="3 3" stroke="#2a2a2a" />
            <XAxis
              dataKey="ply"
              stroke="#888"
              fontSize={12}
              tickLine={false}
              axisLine={false}
            />
            <YAxis
              domain={[0, 100]}
              stroke="#888"
              fontSize={12}
              tickLine={false}
              axisLine={false}
              tickFormatter={(value) => `${value}%`}
            />
            <Tooltip content={<CustomTooltip />} />
            <ReferenceLine y={50} stroke="#444" strokeDasharray="3 3" />
            <Line
              type="monotone"
              dataKey="winPct"
              stroke="#96bc4b"
              strokeWidth={3}
              dot={(props) => {
                const { cx, cy, payload } = props;
                if (payload.ply === currentPly) {
                  return (
                    <circle
                      cx={cx}
                      cy={cy}
                      r={6}
                      fill="#1baca6"
                      stroke="#fff"
                      strokeWidth={2}
                    />
                  );
                }
                return null;
              }}
              activeDot={{ r: 8, stroke: '#96bc4b', strokeWidth: 2, fill: '#fff' }}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};
