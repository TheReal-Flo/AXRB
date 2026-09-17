import { Button } from '@/components/ui/button';
import { bytes, call, Empty } from './common';

export function Downloads({ jobs, run, pending }) {
  return <div className="jobs mx-auto max-w-3xl">{jobs.length ? jobs.map(job => <article key={job.id} className="border-b py-5 first:pt-0">
    <div className="flex items-center justify-between gap-6"><div className="min-w-0 flex-1"><div className="flex items-baseline justify-between gap-4"><span className="truncate font-medium">{job.name}</span><span className="shrink-0 text-xs capitalize text-muted-foreground">{job.status}</span></div>
      <div className="mt-1 flex justify-between gap-4 text-xs text-muted-foreground"><span className="truncate">{job.stage}</span>{Boolean(job.total) && <span className="shrink-0 tabular-nums">{bytes(job.completed)} / {bytes(job.total)}</span>}</div>
      {job.status === 'downloading' && <progress className="mt-3 block" value={job.total ? job.completed || 0 : undefined} max={job.total || undefined} aria-label={`${job.name} download progress`} />}
    </div>
      {['queued', 'downloading'].includes(job.status) ? <Button variant="outline" size="sm" disabled={pending.has(job.id)} onClick={() => run(job.id, () => call('cancel', job.id))}>Cancel</Button> : ['failed', 'interrupted', 'cancelled'].includes(job.status) && /^\d+$/.test(job.gameId) ? <Button variant="outline" size="sm" disabled={pending.has(job.id)} onClick={() => run(job.id, () => call('retry', job.id))}>Retry</Button> : null}
    </div>{job.error && <p className="mt-3 break-words text-xs text-destructive">{job.error}</p>}
  </article>) : <Empty>No downloads</Empty>}</div>;
}
