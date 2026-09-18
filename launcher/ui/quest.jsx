import { useEffect, useRef, useState } from 'react';
import { Download, RefreshCw, Search } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { call, Empty, IconButton } from './common';

export function Quest({ state, run, pending, setPage }) {
  const [devices, setDevices] = useState([]), [serial, setSerial] = useState('');
  const [games, setGames] = useState([]), [query, setQuery] = useState(''), [loaded, setLoaded] = useState(false);
  const request = useRef(0);
  const refresh = () => run('quest-devices', async () => {
    const found = await call('questDevices'); setDevices(found); setLoaded(true);
    const next = found.some(d => d.serial === serial) ? serial : found[0]?.serial || '';
    setSerial(next);
    if (!next) setGames([]);
    if (next && next === serial && found.find(d => d.serial === next)?.status === 'device') await loadGames(next);
  });
  const loadGames = async selected => {
    const generation = ++request.current; setGames([]);
    const result = await call('questGames', selected);
    if (request.current === generation) setGames(result);
  };
  useEffect(() => { refresh(); return () => { request.current++; }; }, []);
  const device = devices.find(d => d.serial === serial);
  useEffect(() => {
    request.current++; setGames([]);
    if (device?.status === 'device') run(`quest-games-${serial}`, () => loadGames(serial));
  }, [serial, device?.status]);
  const visible = games.filter(g => `${g.name} ${g.package}`.toLowerCase().includes(query.toLowerCase()));
  return <div className="mx-auto max-w-4xl" data-quest>
    <div className="mb-6 flex items-center gap-3">
      <Select value={serial} onValueChange={setSerial} disabled={!devices.length}><SelectTrigger aria-label="Quest device" className="w-64"><SelectValue placeholder="No Quest connected" /></SelectTrigger><SelectContent>{devices.map(d => <SelectItem key={d.serial} value={d.serial}>{d.name} · {d.serial}</SelectItem>)}</SelectContent></Select>
      <div className="relative flex-1"><Search className="pointer-events-none absolute top-2.5 left-3 size-4 text-muted-foreground" /><Input aria-label="Search Quest games" placeholder="Search Quest" className="pl-9" value={query} onChange={e => setQuery(e.target.value)} /></div>
      <IconButton label="Refresh Quest" disabled={pending.has('quest-devices')} onClick={refresh}><RefreshCw className={pending.has('quest-devices') ? 'animate-spin' : ''} /></IconButton>
    </div>
    {!devices.length ? <Empty>{loaded ? 'Connect your Quest by USB, enable developer mode, and allow USB debugging in the headset.' : 'Checking devices…'}</Empty>
      : device?.status !== 'device' ? <Empty>{device?.status === 'unauthorized' ? 'Allow USB debugging inside your Quest, then refresh.' : 'Quest is offline. Reconnect it and refresh.'}</Empty>
      : pending.has(`quest-games-${serial}`) ? <Empty>Reading Quest…</Empty>
      : !visible.length ? <Empty>No matching apps</Empty>
      : visible.map(game => {
        const known = state.games.find(g => g.package === game.package);
        return <div key={game.package} className="flex items-center gap-4 border-b py-4">
          <div className="min-w-0 flex-1"><div className="truncate text-sm">{known?.name || game.name}</div>{(known?.name || game.name) !== game.package && <div className="truncate text-xs text-muted-foreground">{game.package}</div>}</div>
          <Button variant="outline" size="sm" disabled={state.busy || pending.has('quest-import')} onClick={() => run('quest-import', async () => { await call('questImport', serial, game.package); setPage('downloads'); })}><Download />{known?.downloaded ? 'Import again' : 'Import'}</Button>
        </div>;
      })}
  </div>;
}
