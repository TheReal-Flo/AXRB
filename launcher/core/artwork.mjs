// Keep the library usable while fetching artwork, with bounded requests and a
// retry delay for unavailable/delisted apps. CDN URLs are refreshed periodically.
export async function loadLibraryArtwork(games, api, update, now = Date.now()) {
  const pending = games.filter(g => g.source === 'meta' && /^\d+$/.test(g.id) &&
    now - (g.artworkCheckedAt || 0) >= (g.image ? 7 * 86400000 : 3600000));
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(3, pending.length) }, async () => {
    while (next < pending.length) {
      const game = pending[next++];
      let image = '';
      try { image = await api.artwork(game.id); } catch { /* Preserve cached art on network/API failures. */ }
      await update(game.id, { ...(image ? { image } : {}), artworkCheckedAt: now });
    }
  }));
}
