// Opt-in live validation. Uses the launcher's encrypted session locally; never
// prints tokens, signed file URLs, or account identifiers.
import { app, safeStorage } from 'electron';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { QuestStore } from './core/meta.mjs';
const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const profile = path.join(app.getPath('appData'), 'AXRB');
// safeStorage needs the original profile's encryption key, not a test profile.
app.setPath('userData', profile);
app.whenReady().then(async () => {
  const token = safeStorage.decryptString(await fs.readFile(path.join(profile, 'meta-session.bin')));
  const api = new QuestStore(token), report = {};
  try {
    const library = await api.library();
    report.library = { count: library.games.length, partial: library.partial, games: library.games.map(g => ({ id:g.id, name:g.name, platform:g.platform })) };
    const id = process.argv.find(a => /^--app=\d+$/.test(a))?.split('=')[1];
    if (id) {
      try { const builds = await api.builds(id); report.builds = builds.slice(0, 5).map(b => ({ id:b.id, version:b.version, platform:b.platform, code:b.version_code })); }
      catch (e) { report.buildError = e.message; }
      try { const plan = await api.plan(id); report.plan = { ...plan, files:plan.files.map(({ uri, ...file }) => file) }; }
      catch (e) { report.planError = e.message; }
      try { const dlc = await api.dlc(id); report.dlc = dlc.map(d => ({id:d.id,name:d.name,owned:d.owned,files:d.files.map(({uri,...f})=>f)})); }
      catch (e) { report.dlcError = e.message; }
    }
  } catch (error) { report.error = error.message; }
  await fs.mkdir(path.join(root,'out/launcher/validation'),{recursive:true});
  await fs.writeFile(path.join(root,'out/launcher/validation/meta-report.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify(report));app.quit();
}).catch(() => { console.error('Live validation could not open the encrypted Meta session. Sign in first.'); app.exit(1); });
