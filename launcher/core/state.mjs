import fs from 'node:fs/promises';
import path from 'node:path';
export class State {
  constructor(directory) { this.directory = directory; this.data = { games: [], jobs: [], settings: {} }; this.writes = Promise.resolve(); }
  async load() {
    await fs.mkdir(this.directory, { recursive: true });
    try { this.data = { ...this.data, ...JSON.parse(await fs.readFile(path.join(this.directory, 'library.json'), 'utf8')) }; }
    catch (error) { if (error.code !== 'ENOENT') throw new Error('Launcher library could not be read. The original file has been preserved.'); }
    this.data.jobs = this.data.jobs.map(j => ['queued', 'downloading', 'installing', 'patching', 'importing', 'uninstalling'].includes(j.status) ? { ...j, status: 'interrupted', error: 'Interrupted when the launcher closed. Retry to continue.' } : j);
  }
  save() {
    const snapshot = JSON.stringify(this.data, null, 2);
    const write = async () => {
      const file = path.join(this.directory, 'library.json');
      await fs.writeFile(`${file}.tmp`, snapshot);
      await fs.rename(`${file}.tmp`, file);
    };
    this.writes = this.writes.catch(() => {}).then(write);
    return this.writes;
  }
  put(game) {
    const existing = this.data.games.find(g => g.id === game.id) || this.data.games.find(g => game.package && g.package === game.package);
    if (game.package) {
      const duplicate = this.data.games.find(g => g !== existing && g.package === game.package);
      if (duplicate && existing) { Object.assign(existing, duplicate, { id: existing.id }); this.data.games = this.data.games.filter(g => g !== duplicate); }
    }
    if (existing) {
      const image = game.image || existing.image;
      Object.assign(existing, game);
      if (image) existing.image = image;
    } else this.data.games.push(game);
    return existing || game;
  }
}
