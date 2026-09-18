import { useEffect, useRef, useState } from 'react';
import { Check, Loader2, X } from 'lucide-react';
import { activeStatuses, bytes } from './common';
import { cn } from '@/lib/utils';

export function Toasts({ jobs, notice, dismissNotice }) {
  const [items, setItems] = useState([]);
  const seen = useRef(new Map()), dismissed = useRef(new Map()), timers = useRef(new Map());
  const initialized = useRef(false);
  const dismiss = id => {
    dismissed.current.set(id, seen.current.get(id));
    clearTimeout(timers.current.get(id)); timers.current.delete(id);
    setItems(current => current.filter(j => j.id !== id));
  };
  useEffect(() => {
    if (!jobs) return;
    const changes = [];
    for (const job of jobs) {
      if (!['apk', 'zip', 'quest', 'uninstall'].includes(job.kind)) continue;
      const previous = seen.current.get(job.id), active = activeStatuses.includes(job.status);
      seen.current.set(job.id, job.status);
      if (!initialized.current && !active) continue;
      if (dismissed.current.get(job.id) === job.status) continue;
      if (!active && previous === job.status) continue;
      changes.push(job);
      if (!active && job.status !== 'failed' && job.status !== 'interrupted') {
        clearTimeout(timers.current.get(job.id));
        timers.current.set(job.id, setTimeout(() => dismiss(job.id), 6000));
      }
    }
    initialized.current = true;
    if (changes.length) setItems(current => [...changes, ...current.filter(j => !changes.some(n => n.id === j.id))].slice(0, 4));
  }, [jobs]);
  useEffect(() => () => { for (const timer of timers.current.values()) clearTimeout(timer); }, []);
  return <div aria-label="Notifications" className="fixed right-6 bottom-6 z-[100] flex w-96 max-w-[calc(100vw-3rem)] flex-col gap-2">
    {items.map(job => {
      const active = activeStatuses.includes(job.status), error = ['failed', 'interrupted'].includes(job.status);
      const title = error ? `${job.name}: ${job.kind === 'uninstall' ? 'uninstall' : 'import'} failed`
        : job.status === 'cancelled' ? `${job.name}: import cancelled`
        : job.status === 'complete' ? `${job.name}: ${job.kind === 'uninstall' ? 'uninstalled' : job.kind === 'zip' ? 'installed' : 'imported'}` : job.name;
      return <div key={job.id} data-job-toast={job.id} role={error ? 'alert' : 'status'} className={cn('rounded-md border bg-popover p-4 shadow-lg', error && 'text-destructive')}>
        <div className="flex items-start gap-3">{active ? <Loader2 className="mt-0.5 size-4 shrink-0 animate-spin" aria-hidden="true" /> : job.status === 'complete' ? <Check className="mt-0.5 size-4 shrink-0" aria-hidden="true" /> : null}
          <div className="min-w-0 flex-1"><div className="break-words text-sm font-medium">{title}</div>{(active || error) && <div className="mt-1 break-words text-xs text-muted-foreground">{error ? job.error : job.stage}</div>}</div>
          <button aria-label={`Dismiss ${job.name} notification`} className="shrink-0" onClick={() => dismiss(job.id)}><X className="size-4" /></button>
        </div>
        {active && job.total > 0 && job.status !== 'installing' && <><progress className="mt-3 block w-full" value={job.completed || 0} max={job.total} aria-label={`${job.name} import progress`} /><div className="mt-1 text-right text-xs tabular-nums text-muted-foreground">{job.progressUnit === 'files' ? `${job.completed || 0} / ${job.total} files` : `${bytes(job.completed)} / ${bytes(job.total)}`}</div></>}
      </div>;
    })}
    {notice && <div role={notice.error ? 'alert' : 'status'} className={cn('flex items-start gap-4 rounded-md border bg-popover px-4 py-3 shadow-lg', notice.error && 'text-destructive')}><span className="min-w-0 flex-1 break-words">{notice.text}</span><button aria-label="Dismiss notification" onClick={dismissNotice} className="mt-0.5 shrink-0"><X className="size-4" /></button></div>}
  </div>;
}
