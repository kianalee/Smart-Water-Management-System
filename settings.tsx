import { useEffect, useState } from "react";

type DistanceReading = {
  id: number;
  value: number;
  timestamp: string;
};

export default function Settings() {
  const [data, setData] = useState<DistanceReading[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetch("http://localhost:3000/api/distance")
      .then((res) => {
        if (!res.ok) {
          throw new Error("Network response was not ok");
        }
        return res.json();
      })
      .then((json) => {
        console.log("API response:", json);
        setData(json);
        setLoading(false);
      })
      .catch((err) => {
        console.error(err);
        setLoading(false);
      });
  }, []);

  if (loading) return <div>Loading…</div>;

  return (
    <div className="mainContent">
      <h1>Distance Readings</h1>
      <ul>
        {data.map((reading) => (
          <li key={reading.id}>
            {reading.timestamp}: {reading.value} cm
          </li>
        ))}
      </ul>
    </div>
  );
}
