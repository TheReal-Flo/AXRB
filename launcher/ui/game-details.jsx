import { useRef, useState } from 'react';
import { Download, ExternalLink, Loader2, MoreHorizontal, Plus } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Dialog, DialogContent, DialogTitle } from '@/components/ui/dialog';
import { DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuSeparator, DropdownMenuTrigger } from '@/components/ui/dropdown-menu';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { activeStatuses, bytes, call, Cover, IconButton } from './common';

export function GameDetails({ game, state, local, onClose, run, pending, setPage, notify }) {
  const returnFocus = useRef(document.activeElement);
  const [extra, setExtra] = useState(null);
  const [build, setBuild] = useState('');
  const busy = state.busy || pending.has(`game-${game.id}`);
  const activeJob = state.jobs.find(j => j.gameId === game.id && activeStatuses.includes(j.status));
  const running = state.running === game.id;
  const operate = fn => run(`game-${game.id}`, fn);
  const download = () => operate(async () => { await call('download', game.id, build || undefined); onClose(); setPage('downloads'); });
  const install = () => operate(async () => { await call('install', game.id); notify('Installed'); });
  const loadExtra = kind => operate(async () => { const items = await call(kind, game.id); setExtra({ kind, items }); if (kind === 'builds') setBuild(items[0]?.id || ''); });
  return <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
    <DialogContent id="details" aria-describedby={undefined} onCloseAutoFocus={event => { event.preventDefault(); if (returnFocus.current?.isConnected) returnFocus.current.focus(); }} className="max-h-[85vh] gap-0 overflow-y-auto p-0 sm:max-w-[520px]">
      <div className="px-6 pt-6 pr-14"><DialogTitle className="leading-snug">{game.name}</DialogTitle>{game.version && <div className="mt-1 text-xs text-muted-foreground">{game.version}</div>}</div>
      <div className="space-y-5 p-6">
        <Cover game={game} className="aspect-video" />
        <div className="flex items-center gap-2">
          {activeJob ? <Button variant="secondary" onClick={() => { onClose(); setPage('downloads'); }}><Loader2 className="animate-spin" />{activeJob.status === 'downloading' ? 'Downloading' : activeJob.status === 'installing' ? 'Installing' : 'Queued'}</Button>
            : running ? <Button disabled={busy} onClick={() => operate(async () => { await call('stop'); notify('Closing game'); })}>Stop</Button>
            : game.installed ? <Button className="min-w-24" disabled={busy || Boolean(state.running)} onClick={() => operate(async () => { await call('play', game.id); onClose(); })}>Play</Button>
            : game.apk ? <Button disabled={busy} onClick={install}>Install</Button>
            : !local ? <Button disabled={busy} onClick={() => operate(() => call('add', game.id))}><Plus />Add to library</Button>
            : !state.signedIn ? <Button disabled={pending.has('account')} onClick={() => run('account', () => call('login'))}>Connect Meta</Button>
            : <Button disabled={busy} onClick={download}><Download />Download</Button>}
          <div className="flex-1" />
          {game.source === 'meta' && <IconButton label="View on Meta" onClick={() => operate(() => call('openStore', game.id))}><ExternalLink /></IconButton>}
          {local && <DropdownMenu><DropdownMenuTrigger asChild><Button variant="ghost" size="icon" aria-label="Game actions" title="Game actions"><MoreHorizontal /></Button></DropdownMenuTrigger>
            <DropdownMenuContent align="end">
              {game.source === 'meta' && <><DropdownMenuItem disabled={busy || !state.signedIn} onSelect={() => loadExtra('builds')}>Versions</DropdownMenuItem><DropdownMenuItem disabled={busy || !state.signedIn} onSelect={() => loadExtra('dlc')}>Add-ons</DropdownMenuItem></>}
              {game.apk && <>{game.source === 'meta' && <DropdownMenuSeparator />}<DropdownMenuItem disabled={busy} onSelect={() => operate(async () => { await call('patch', game.id); notify('Patched'); })}>Patch with ovrport</DropdownMenuItem><DropdownMenuItem disabled={busy} onSelect={() => operate(async () => { await call('importAssets', game.id); notify('Install to apply content files'); })}>Add content files</DropdownMenuItem><DropdownMenuItem onSelect={() => operate(() => call('openFolder', game.id))}>Open folder</DropdownMenuItem>{game.installed && <DropdownMenuItem disabled={busy} onSelect={install}>Update installation</DropdownMenuItem>}</>}
              {!game.apk && game.source !== 'meta' && <DropdownMenuItem disabled={busy} onSelect={() => run('import', () => call('import'))}>Import APK</DropdownMenuItem>}
            </DropdownMenuContent>
          </DropdownMenu>}
        </div>
        {pending.has(`game-${game.id}`) && <div role="status" className="flex items-center gap-2 text-xs text-muted-foreground"><Loader2 className="size-3 animate-spin" />Working…</div>}
        {extra?.kind === 'builds' && <div className="flex items-center gap-2 border-t pt-4">{extra.items.length ? <><Select value={build} onValueChange={setBuild}><SelectTrigger className="min-w-0 flex-1" aria-label="Quest build"><SelectValue /></SelectTrigger><SelectContent>{extra.items.map(b => <SelectItem key={b.id} value={b.id}>{b.version || b.code}</SelectItem>)}</SelectContent></Select><Button disabled={busy || Boolean(activeJob)} onClick={download}>Download</Button></> : <span className="text-muted-foreground">No builds available</span>}</div>}
        {extra?.kind === 'dlc' && <div className="border-t pt-4">{extra.items.length ? <><p className="mb-3 text-xs text-muted-foreground">Install after downloading to apply add-ons.</p>{extra.items.map(dlc => <div key={dlc.id} className="flex items-center justify-between gap-3 py-2"><div className="min-w-0"><div className="text-sm">{dlc.name}</div><div className="mt-1 text-xs text-muted-foreground">{!dlc.owned ? 'Ownership not confirmed' : !dlc.fileCount ? 'No separate download' : bytes(dlc.bytes)}</div></div><Button size="sm" variant="outline" disabled={!dlc.owned || !dlc.fileCount || busy || Boolean(activeJob)} onClick={() => operate(async () => { await call('downloadDlc', game.id, dlc.id); onClose(); setPage('downloads'); })}>Download</Button></div>)}</> : <p className="text-muted-foreground">No downloadable add-ons</p>}</div>}
      </div>
    </DialogContent>
  </Dialog>;
}
