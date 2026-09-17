import { useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { activeStatuses, call } from './common';

export function Settings({ state, run, pending, notify }) {
  const [draft, setDraft] = useState({ ...state.settings });
  const [dirty, setDirty] = useState(false);
  const edit = (key, value) => { setDraft(d => ({ ...d, [key]: value })); setDirty(true); };
  const browse = key => run(`choose-${key}`, async () => {
    const value = await call(key === 'ovrportCli' ? 'chooseCli' : 'chooseFolder');
    if (value) edit(key, value);
  });
  const field = (key, label, options = {}) => <div className="space-y-2">
    <label htmlFor={key}>{label}</label>
    <div className="flex gap-2"><Input id={key} name={key} value={draft[key] ?? ''} onChange={e => edit(key, e.target.value)} {...options} />
      {['downloadDir', 'ovrportCli'].includes(key) && <Button type="button" variant="outline" disabled={pending.has(`choose-${key}`)} onClick={() => browse(key)}>Browse</Button>}
    </div>
  </div>;
  return <form id="settings-form" className="max-w-2xl space-y-7" onSubmit={e => {
    e.preventDefault(); run('settings', async () => {
      await call('settings', { ...draft, port: Number(draft.port), memoryMB: Number(draft.memoryMB) });
      setDirty(false); notify('Saved');
    });
  }}>
    {field('downloadDir', 'Download folder', { required: true })}
    {field('ovrportCli', 'ovrport CLI', { placeholder: 'Optional .exe or .jar' })}
    <div className="flex items-center justify-between border-y py-4"><span>Meta{state.signedIn && <span className="ml-3 text-muted-foreground">Connected</span>}</span>
      <Button type="button" variant="outline" disabled={pending.has('account')} onClick={() => run('account', () => call(state.signedIn ? 'logout' : 'login'))}>{state.signedIn ? 'Sign out' : 'Connect'}</Button>
    </div>
    <details className="group" data-runtime-settings><summary className="flex cursor-pointer list-none items-center justify-between py-1">Android runtime<ChevronDown className="size-4 transition-transform group-open:rotate-180" /></summary>
      <div className="mt-5 space-y-5">{field('sdk', 'Android SDK', { required: true })}{field('avd', 'Virtual device', { required: true, pattern: '[a-zA-Z0-9_-]+' })}
        <div className="grid grid-cols-2 gap-4">{field('port', 'Port', { type: 'number', min: 5554, max: 5682, step: 2, required: true })}{field('memoryMB', 'Memory (MB)', { type: 'number', min: 2048, max: 16384, step: 1024, required: true })}</div>
      </div>
    </details>
    <Button type="submit" disabled={!dirty || state.busy || Boolean(state.running) || pending.has('settings') || state.jobs.some(j => activeStatuses.includes(j.status))}>Save</Button>
  </form>;
}
