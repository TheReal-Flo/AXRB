import { useEffect, useState } from 'react';
import { Check } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { cn } from '@/lib/utils';

export const activeStatuses = ['queued', 'downloading', 'installing', 'patching', 'importing', 'uninstalling'];
export const bytes = n => n ? `${(n / 1024 ** 3).toFixed(n < 1024 ** 3 ? 2 : 1)} GB` : '0 GB';
export async function call(method, ...args) {
  const result = await window.axrb[method](...args);
  if (!result.ok) throw new Error(result.error);
  return result.value;
}
function safeImage(value) {
  try {
    const url = new URL(value);
    if (url.protocol === 'data:' && value.startsWith('data:image/png;base64,')) return value;
    if (url.protocol === 'https:' && ['oculuscdn.com', 'fbcdn.net', 'oculus.com', 'meta.com'].some(d => url.hostname === d || url.hostname.endsWith(`.${d}`))) return value;
  } catch {}
  return '';
}
export function IconButton({ label, children, ...props }) {
  return <Button variant="ghost" size="icon" aria-label={label} title={label} {...props}>{children}</Button>;
}
export function Cover({ game, className }) {
  const src = safeImage(game.image);
  const [failed, setFailed] = useState(false);
  useEffect(() => setFailed(false), [src]);
  return <div className={cn('flex aspect-[4/3] items-center justify-center overflow-hidden rounded-md bg-secondary', className)}>
    {src && !failed ? <img src={src} alt="" loading="lazy" className="h-full w-full object-cover" onError={() => setFailed(true)} /> : <span className="text-3xl text-muted-foreground" aria-hidden="true">{game.name?.split(/\s+/).slice(0, 2).map(w => w[0]).join('')}</span>}
  </div>;
}
export function GameGrid({ games, onOpen, store = false }) {
  return <div className="grid grid-cols-[repeat(auto-fill,minmax(190px,1fr))] gap-x-6 gap-y-7">
    {games.map(game => <button key={game.id} data-game={game.id} data-store={store || undefined} onClick={() => onOpen(game.id)} className="group min-w-0 rounded-md text-left" aria-label={`Open ${game.name}`}>
      <Cover game={game} className="transition-opacity group-hover:opacity-85" />
      <div className="mt-2.5 truncate font-medium" title={game.name}>{game.name}</div>
      <div className="mt-0.5 flex min-h-5 items-center gap-1.5 text-xs text-muted-foreground">
        {store ? (!game.price || parseFloat(String(game.price).replace(/[^\d.]/g, '')) === 0 ? 'FREE' : game.price) : game.installed ? <><Check className="size-3" />Installed</> : game.downloaded ? 'Downloaded' : null}
      </div>
    </button>)}
  </div>;
}
export function Empty({ children }) { return <p className="py-24 text-center text-muted-foreground">{children}</p>; }
