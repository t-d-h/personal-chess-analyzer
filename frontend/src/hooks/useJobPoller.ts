import { useEffect, useState } from 'react';
import { getJob } from '../services/api';
import type { JobStatus } from '../services/api';

export function useJobPoller(jobId: string | null) {
  const [jobState, setJobState] = useState<JobStatus | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!jobId) {
      setJobState(null);
      setError(null);
      return;
    }

    setJobState(null);
    setError(null);

    let intervalId: any = null;

    const poll = async () => {
      try {
        const data = await getJob(jobId);
        setJobState(data);
        if (data.status === 'completed' || data.status === 'failed') {
          clearInterval(intervalId);
        }
      } catch (err: any) {
        setError(err.message || 'Failed to fetch job status');
        clearInterval(intervalId);
      }
    };

    poll();
    intervalId = setInterval(poll, 1500);

    return () => {
      if (intervalId) {
        clearInterval(intervalId);
      }
    };
  }, [jobId]);

  return {
    jobState,
    error: error || jobState?.error || null,
  };
}
