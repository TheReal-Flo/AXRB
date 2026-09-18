import { useCallback, useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { ExternalLink, Loader2, Plus, RefreshCw, Search, Settings as SettingsIcon } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { cn } from '@/lib/utils';
import { activeStatuses, call, Empty, GameGrid, IconButton } from './common';
import { SetupScreen } from './setup';
import { Settings } from './settings';
import { Downloads } from './downloads';
import { GameDetails } from './game-details';
import { Quest } from './quest';
import { Toasts } from './toasts';
import './style.css';

function App() {
  const [state, setState] = useState(null);
  const [page, setPage] = useState('library');
  const [filter, setFilter] = useState('all');
  const [libraryQuery, setLibraryQuery] = useState('');
  const [query, setQuery] = useState('');
  const [results, setResults] = useState([]);
  const [searched, setSearched] = useState(false);
  const [selected, setSelected] = useState(null);
  const [pending, setPending] = useState(new Set());
  const pendingRef = useRef(new Set());
  const [notice, setNotice] = useState(null);
  const timer = useRef(null);
  const notify = useCallback((text, error = false) => {
    clearTimeout(timer.current); setNotice({ text, error });
    if (!error) timer.current = setTimeout(() => setNotice(null), 4000);
  }, []);
  const run = useCallback(async (key, task) => {
    if (pendingRef.current.has(key)) return;
    pendingRef.current.add(key); setPending(new Set(pendingRef.current));
    try { return await task(); } catch (error) { notify(error.message, true); }
    finally { pendingRef.current.delete(key); setPending(new Set(pendingRef.current)); }
  }, [notify]);
  useEffect(() => {
    let active = true;
    const off = window.axrb.onChange(next => { if (active) setState(next); });
    const offError = window.axrb.onLaunchError(error => notify(error, true));
    call('state').then(value => { if (active) setState(value); }).catch(error => notify(error.message, true));
    return () => { active = false; off(); offError(); clearTimeout(timer.current); };
  }, [notify]);
  const refresh = () => run('sync', async () => {
    const info = await call('sync');
    if (info.meta?.partial) notify('Meta returned a partial library. Add other games from the store.');
  });
  const search = e => {
    e.preventDefault(); const text = query.trim(); if (text.length < 2) return;
    run('search', async () => { const games = await call('search', text); setResults(games); setSearched(true); });
  };
  const game = state?.games.find(g => g.id === selected) || results.find(g => g.id === selected);
  const games = state?.games.filter(g => (filter === 'all' || Boolean(g[filter])) && g.name.toLowerCase().includes(libraryQuery.toLowerCase())) || [];
  const activeDownloads = state?.jobs.filter(j => activeStatuses.includes(j.status)).length || 0;
  if (state?.setup && state.setup.phase !== 'ready') return <SetupScreen setup={state.setup} />;
  return <>
    <header className="flex h-16 items-center gap-8 border-b px-8">
      <button onClick={() => setPage('library')} aria-label="AXRB library" className="text-base font-semibold tracking-wide">AXRB</button>
      <nav aria-label="Main navigation" className="flex h-full items-center gap-6">{[['library', 'Library'], ['store', 'Store'], ['quest', 'Quest'], ['downloads', 'Downloads']].map(([id, label]) => <button key={id} data-nav={id} aria-current={page === id ? 'page' : undefined} onClick={() => setPage(id)} className={cn('relative flex h-full items-center gap-2 text-sm text-muted-foreground hover:text-foreground', page === id && 'text-foreground after:absolute after:inset-x-0 after:bottom-0 after:h-0.5 after:bg-foreground')}>{label}{id === 'downloads' && activeDownloads > 0 && <span className="rounded bg-secondary px-1.5 text-xs">{activeDownloads}</span>}</button>)}</nav>
      <div className="ml-auto flex items-center gap-2">{state && !state.signedIn && <Button variant="ghost" disabled={pending.has('account')} onClick={() => run('account', () => call('login'))}>{pending.has('account') ? 'Connecting…' : 'Connect Meta'}</Button>}<IconButton label="Settings" data-nav="settings" aria-pressed={page === 'settings'} onClick={() => setPage('settings')}><SettingsIcon /></IconButton></div>
    </header>
    <main id="content" className="mx-auto max-w-[1600px] p-8" aria-label={page}>
      {!state ? <Empty><Button variant="ghost" onClick={() => run('state', async () => setState(await call('state')))}>Load library</Button></Empty> : <>
        {page === 'library' && <><div className="mb-7 flex items-center gap-3"><div className="relative w-64"><Search className="pointer-events-none absolute top-2.5 left-3 size-4 text-muted-foreground" /><Input id="library-search" type="search" placeholder="Search library" aria-label="Search library" className="pl-9" value={libraryQuery} onChange={e => setLibraryQuery(e.target.value)} /></div>
          <Select value={filter} onValueChange={setFilter}><SelectTrigger aria-label="Filter library" className="w-36"><SelectValue /></SelectTrigger><SelectContent><SelectItem value="all">All games</SelectItem><SelectItem value="installed">Installed</SelectItem><SelectItem value="downloaded">Downloaded</SelectItem></SelectContent></Select>
          <div className="flex-1" /><IconButton label="Refresh library" disabled={pending.has('sync')} onClick={refresh}><RefreshCw className={pending.has('sync') ? 'animate-spin' : ''} /></IconButton><Button variant="outline" disabled={state.busy || pending.has('import')} onClick={() => run('import', () => call('import'))}><Plus />Import APK</Button><Button variant="outline" disabled={state.busy || Boolean(state.running) || pending.has('import-zip')} onClick={() => run('import-zip', async () => { if (await call('importZip')) setPage('downloads'); })}>Install ZIP</Button>
        </div>{games.length ? <GameGrid games={games} onOpen={setSelected} /> : <Empty>{state.games.length ? 'No matching games' : 'No games yet'}</Empty>}</>}
        {page === 'store' && <><form id="store-search" onSubmit={search} className="mb-7 flex items-center gap-3"><div className="relative w-full max-w-md"><Search className="pointer-events-none absolute top-2.5 left-3 size-4 text-muted-foreground" /><Input id="store-query" type="search" aria-label="Search Quest store" placeholder="Search Quest games" className="pl-9" value={query} onChange={e => setQuery(e.target.value)} required minLength={2} /></div><Button disabled={pending.has('search')} type="submit">{pending.has('search') ? <Loader2 className="animate-spin" aria-label="Searching" /> : 'Search'}</Button><div className="flex-1" /><IconButton label="Open Meta store" onClick={() => run('store', () => call('openStore'))}><ExternalLink /></IconButton></form>{results.length ? <GameGrid games={results} onOpen={setSelected} store /> : searched && !pending.has('search') ? <Empty>No games found</Empty> : null}</>}
        {page === 'quest' && <Quest state={state} run={run} pending={pending} setPage={setPage} />}
        {page === 'downloads' && <Downloads jobs={state.jobs} run={run} pending={pending} />}
        {page === 'settings' && <Settings state={state} run={run} pending={pending} notify={notify} />}
      </>}
    </main>
    <Toasts jobs={state?.jobs} notice={notice} dismissNotice={() => setNotice(null)} />
    {game && state && <GameDetails key={game.id} game={game} state={state} local={state.games.some(g => g.id === game.id)} onClose={() => setSelected(null)} run={run} pending={pending} setPage={setPage} notify={notify} />}
  </>;
}

createRoot(document.getElementById('root')).render(<App />);
