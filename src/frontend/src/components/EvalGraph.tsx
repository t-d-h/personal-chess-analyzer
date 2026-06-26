import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, ReferenceLine } from "recharts";
import type { MoveData } from "../services/api";

interface Props {
  moves: MoveData[];
}

function cpToWinPct(cp: number): number {
  return 50 + 50 * (2 / (1 + Math.exp(-0.00368208 * cp)) - 1);
}

interface DataPoint {
  ply: number;
  winPct: number;
}

export function EvalGraph({ moves }: Props) {
  const data: DataPoint[] = [{ ply: 0, winPct: 50 }];

  for (const move of moves) {
    const cp = move.evalCpPlayed ?? 0;
    const capped = Math.max(-10000, Math.min(10000, move.evalMate ? (move.evalMate > 0 ? 10000 : -10000) : cp));
    const isBlack = move.color === "black";
    const winPct = cpToWinPct(isBlack ? -capped : capped);
    data.push({ ply: move.ply, winPct: Math.round(winPct * 10) / 10 });
  }

  return (
    <div className="eval-graph-container">
      <h3>Eval Graph</h3>
      <ResponsiveContainer width="100%" height={200}>
        <LineChart data={data} margin={{ top: 5, right: 20, bottom: 5, left: 10 }}>
          <XAxis dataKey="ply" stroke="#888" fontSize={11} />
          <YAxis domain={[0, 100]} stroke="#888" fontSize={11} tickFormatter={(v: number) => `${v}%`} />
          <ReferenceLine y={50} stroke="#444" strokeDasharray="3 3" />
          <Tooltip
            contentStyle={{ background: "#1a1a2e", border: "1px solid #333", borderRadius: 4 }}
            labelFormatter={(l: number) => `Ply ${l}`}
            formatter={(v: number) => [`${v}%`, "Win %"]}
          />
          <Line type="monotone" dataKey="winPct" stroke="#6c9ac3" strokeWidth={2} dot={false} />
        </LineChart>
      </ResponsiveContainer>
    </div>
  );
}
