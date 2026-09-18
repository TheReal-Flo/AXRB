import { useState } from 'react';
import { Loader2 } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { call } from './common';

export function SetupScreen({ setup }) {
  const [directory, setDirectory] = useState(setup.directory.replace(/[\\/]AXRB Runtime$/, '').replace(/^([A-Za-z]:)$/, '$1\\'));
  const [accepted, setAccepted] = useState(false);
  const [storageGB, setStorageGB] = useState(setup.storageGB ?? 32);
  const [error, setError] = useState('');
  const invoke = async (name, value) => { try { setError(''); return await call(name, value); } catch (e) { setError(e.message); } };
  const percent = setup.total ? Math.min(100, Math.floor(setup.completed / setup.total * 100)) : 0;
  const labels = { checking: 'Checking your PC', download: 'Downloading', verify: 'Verifying download', extract: 'Extracting', boot: 'Preparing Android' };
  return <main className="flex min-h-screen items-center justify-center p-10"><section className="w-full max-w-lg space-y-6" aria-label="Runtime setup">
    <h1 className="text-xl font-semibold">AXRB</h1>
    {setup.phase === 'unsupported' ? <>
      <p>This build requires a Windows x64 PC, an AMD or NVIDIA GPU, and at least 12 GB RAM.</p>
      <p className="text-sm text-muted-foreground">{setup.hardware?.gpu} · {setup.hardware?.memoryGB} GB RAM</p>
      <Button variant="outline" onClick={() => invoke('setupCheck')}>Check again</Button>
    </> : setup.phase === 'hypervisor' ? <>
      <p>Enable Windows Hypervisor Platform to run Android.</p>
      <ol className="list-decimal space-y-2 pl-5 text-sm text-muted-foreground"><li>Open Windows Features and enable <strong>Windows Hypervisor Platform</strong>.</li><li>Restart your PC, then reopen AXRB.</li><li>If it remains unavailable, enable Intel VT-x or AMD SVM in your BIOS.</li></ol>
      <div className="flex gap-3"><Button onClick={() => invoke('setupFeatures')}>Windows Features</Button><Button variant="outline" onClick={() => invoke('setupCheck')}>Check again</Button></div>
    </> : setup.active || setup.phase === 'checking' ? <>
      <div className="flex items-center gap-3" role="status"><Loader2 className="size-4 animate-spin" /><span>{labels[setup.phase] || setup.phase}{setup.component ? ` · ${setup.component}` : ''}</span></div>
      {setup.total > 0 && <><progress aria-label="Setup progress" value={setup.completed} max={setup.total} className="h-2 w-full accent-primary" /><div className="flex justify-between text-sm text-muted-foreground"><span>{setup.phase === 'download' ? `${(setup.completed / 1024 ** 2).toFixed(0)} / ${(setup.total / 1024 ** 2).toFixed(0)} MB` : 'Files'}</span><span>{percent}%</span></div></>}
      {setup.phase === 'boot' && <p className="text-sm text-muted-foreground">First boot can take a few minutes.</p>}
      {setup.active && <Button variant="outline" disabled={setup.cancelling} onClick={() => invoke('setupCancel')}>{setup.cancelling ? 'Stopping setup?' : 'Cancel'}</Button>}
    </> : <>
      <p>Download Android 16 and the emulator.</p>
      <div className="space-y-2"><label htmlFor="runtime-folder" className="text-sm">Install folder</label><div className="flex gap-2"><Input id="runtime-folder" value={directory} onChange={e => setDirectory(e.target.value)} /><Button variant="outline" onClick={async () => { const value = await invoke('chooseFolder'); if (value) setDirectory(value); }}>Browse</Button></div></div>
      <div className="space-y-2"><label htmlFor="android-storage" className="text-sm">Android storage (GB)</label><Input id="android-storage" type="number" min="8" max="256" step="8" value={storageGB} onChange={e => setStorageGB(Number(e.target.value))} /></div>
      <p className="text-sm text-muted-foreground">2.4 GB download. Allow {Math.ceil(storageGB * 1.2 + 14)} GB free for setup. Android checks this space before creating its disk. Downloaded APKs need additional space.</p>
      <p className="text-sm text-muted-foreground">SteamVR or another active OpenXR runtime is required to play.</p>
      <label className="flex items-start gap-3 text-sm"><input type="checkbox" checked={accepted} onChange={e => setAccepted(e.target.checked)} className="mt-1" /><span>I accept the <button className="underline" onClick={e => { e.preventDefault(); invoke('setupLicense'); }}>Android SDK license</button>.</span></label>
      <Button disabled={!accepted || !directory || storageGB < 8 || storageGB > 256} onClick={() => invoke('setupStart', { directory, accepted, storageGB })}>{['error', 'cancelled'].includes(setup.phase) ? 'Retry setup' : 'Download and set up'}</Button>
      {setup.phase === 'error' && <Button variant="outline" className="ml-3" onClick={() => invoke('setupCheck')}>Check again</Button>}
      <p className="text-xs text-muted-foreground">ovrport is not included. Import APKs you have patched separately.</p>
    </>}
    {(error || setup.error) && <p role="alert" className="break-words text-sm text-destructive">{error || setup.error}</p>}
  </section></main>;
}
