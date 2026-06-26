import { useEffect, useRef, useState } from "react";
import { getJob, type JobStatus } from "../services/api";

const INITIAL: JobStatus = {
  jobId: "",
  status: "queued",
  movesAnalyzed: 0,
  movesTotal: 0,
  error: null,
};

export function useJobPoller(jobId: string | null): JobStatus | null {
  const [status, setStatus] = useState<JobStatus | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => {
    if (!jobId) {
      setStatus(null);
      return;
    }

    let active = true;

    const poll = async () => {
      try {
        const s = await getJob(jobId);
        if (!active) return;
        setStatus(s);
        if (s.status === "completed" || s.status === "failed") {
          if (intervalRef.current) clearInterval(intervalRef.current);
        }
      } catch {
        if (!active) return;
        setStatus({ ...INITIAL, jobId });
      }
    };

    poll();
    intervalRef.current = setInterval(poll, 1500);

    return () => {
      active = false;
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, [jobId]);

  return status;
}
