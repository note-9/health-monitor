import React, { useEffect, useRef, useState } from "react";
import { Line } from "react-chartjs-2";
import {
  Chart as ChartJS,
  TimeScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
} from "chart.js";

ChartJS.register(TimeScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend);

function App() {
  const [readings, setReadings] = useState([]); // array of {ts, bpm, device_id}
  const wsRef = useRef(null);

  useEffect(() => {
    const backendHost = window.location.hostname; // for same host setups
    const wsUrl = `ws://${backendHost}:8000/ws`;
    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;
    ws.onopen = () => {
      console.log("WS connected");
      // optional: request history
      ws.send(JSON.stringify({ action: "hello" }));
    };
    ws.onmessage = (evt) => {
      try {
        const data = JSON.parse(evt.data);
        // We expect messages that are readings or echoes
        if (data.bpm !== undefined && data.device_id) {
          const item = { ts: Date.now(), bpm: Number(data.bpm), device_id: data.device_id };
          setReadings((r) => {
            const nr = [...r, item].slice(-200);
            return nr;
          });
        }
      } catch (e) {
        // ignore non-json
      }
    };
    ws.onclose = () => console.log("WS closed");
    return () => ws.close();
  }, []);

  const chartData = {
    labels: readings.map(r => new Date(r.ts).toLocaleTimeString()),
    datasets: [
      {
        label: "BPM",
        data: readings.map(r => r.bpm),
        fill: false,
        tension: 0.2,
      },
    ],
  };

  return (
    <div style={{ padding: 20 }}>
      <h2>Heart Rate Monitor — Live</h2>
      <Line data={chartData} />
      <div style={{ marginTop: 12 }}>
        <strong>Latest:</strong>{" "}
        {readings.length ? `${readings[readings.length - 1].bpm} BPM` : "No data yet"}
      </div>
    </div>
  );
}

export default App;
