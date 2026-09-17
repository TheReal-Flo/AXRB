import { app, BrowserWindow, ipcMain, dialog, shell, safeStorage, session } from 'electron';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { randomUUID, createHash } from 'node:crypto';
import { MetaAuth, QuestStore, appId } from './core/meta.mjs';
import { downloadFile, safeName, checkSpace } from './core/download.mjs';
import { State } from './core/state.mjs';
import { Runtime, run } from './core/runtime.mjs';
import { loadLibraryArtwork } from './core/artwork.mjs';

const directory = path.dirname(fileURLToPath(import.meta.url)), root = path.dirname(directory);
const smoke = process.argv.includes('--smoke-test');
if (smoke) app.setPath('userData', path.join(root, 'build-launcher-smoke'));
else app.setPath('userData', path.join(app.getPath('appData'), 'AXRB'));
if (!app.requestSingleInstanceLock()) app.quit();
app.on('second-instance', () => { window?.show(); window?.focus(); });
let window, authWindow, state, runtime, token = '', account = '', busy = false;
const controllers = new Map();
const searchResults = new Map();
let artworkTask;
function refreshArtwork() {
  if (artworkTask) return artworkTask;
  artworkTask = loadLibraryArtwork(state.data.games, new QuestStore(), async (id, artwork) => {
    const game = state.data.games.find(g => g.id === id);
    if (game) { Object.assign(game, artwork); await persist(); }
  }).finally(() => { artworkTask = null; });
  return artworkTask;
}
const uiPath = path.join(directory, 'dist/index.html');
const message = error => String(error?.message || error).replace(/(?:OC|FRL|EA)[A-Za-z0-9_|-]{30,}/g, '[redacted]').replace(/access_token=[^\s&]+/g, 'access_token=[redacted]');
const exists = async file => { try { await fs.access(file); return true; } catch { return false; } };
function publicState() {
  return { ...state.data, signedIn: Boolean(token), account, running: runtime.game, busy,
    // Credentials and signed CDN URLs never reach the renderer or library file.
    games: state.data.games.map(g => ({ ...g, files: g.files?.map(f => ({ name: f.name, path: f.path, kind: f.kind, size: f.size })) })) };
}
function changed() { if (window && !window.isDestroyed()) window.webContents.send('axrb:changed', publicState()); }
async function persist() { await state.save(); changed(); }
function getGame(id) { const game = state.data.games.find(g => g.id === id); if (!game) throw new Error('Game is no longer in your library.'); return game; }
function store() { if (!token) throw new Error('Sign in to Meta first.'); return new QuestStore(token); }
function handler(name, callback) {
  ipcMain.handle(`axrb:${name}`, async (event, ...args) => {
    if (event.sender !== window.webContents || event.senderFrame !== window.webContents.mainFrame) throw new Error('Untrusted launcher request.');
    try { return { ok: true, value: await callback(...args) }; } catch (error) { return { ok: false, error: message(error) }; }
  });
}
async function exclusive(callback) { if (busy) throw new Error('Wait for the current install or patch to finish.'); busy = true; changed(); try { return await callback(); } finally { busy = false; changed(); } }

async function syncInstalled() {
  const installed = await runtime.installed();
  if (!installed) return false;
  state.data.games = state.data.games.filter(g => !(g.source === 'installed' && g.package?.startsWith('com.axrb.')));
  for (const game of state.data.games) game.installed = Boolean(game.package && installed.has(game.package));
  for (const pkg of installed) {
    if (pkg.startsWith('com.axrb.') || pkg.startsWith('com.google.') || state.data.games.some(g => g.package === pkg)) continue;
    try {
      const game = await runtime.importInstalled(pkg, path.join(state.directory, 'icons', `${pkg}.png`));
      if (game) state.put(game);
    } catch { /* Non-launchable packages remain outside the games library. */ }
  }
  await persist(); return true;
}
async function syncMeta() {
  const result = await store().library();
  for (const game of state.data.games) if (game.source === 'meta') game.owned = false;
  for (const game of result.games) state.put(game);
  account = result.name; await persist();
  refreshArtwork().catch(() => {});
  return { partial: result.partial, count: result.games.length };
}
async function login() {
  if (authWindow && !authWindow.isDestroyed()) { authWindow.focus(); return; }
  const auth = new MetaAuth();
  const url = await auth.begin();
  return new Promise((resolve, reject) => {
    const partition = `axrb-meta-${randomUUID()}`;
    const authSession = session.fromPartition(partition);
    authSession.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
    authWindow = new BrowserWindow({ width: 560, height: 800, title: 'Sign in to Meta', parent: window,
      autoHideMenuBar: true, webPreferences: { partition, sandbox: true, contextIsolation: true, nodeIntegration: false } });
    let completed = false, processing = false;
    const navigate = async (event, target) => {
      let dest; try { dest = new URL(target); } catch { event?.preventDefault(); return; }
      if (['oculus:', 'oculus-client:'].includes(dest.protocol)) {
        event?.preventDefault(); if (processing) return; processing = true;
        try {
          const value = await auth.complete(target);
          if (!safeStorage.isEncryptionAvailable()) throw new Error('Windows credential encryption is unavailable.');
          await fs.writeFile(path.join(state.directory, 'meta-session.bin'), safeStorage.encryptString(value));
          token = value; completed = true; authWindow.close(); changed(); resolve();
        } catch (error) { completed = true; authWindow.close(); reject(error); }
      } else if (dest.protocol !== 'https:' || !['meta.com', 'facebook.com', 'oculus.com', 'instagram.com'].some(d => dest.hostname === d || dest.hostname.endsWith(`.${d}`))) event?.preventDefault();
    };
    authWindow.webContents.on('will-navigate', navigate);
    authWindow.webContents.on('will-redirect', navigate);
    authWindow.webContents.setWindowOpenHandler(({ url: target }) => { navigate({ preventDefault() {} }, target); const u = new URL(target); if (u.protocol === 'https:' && ['meta.com', 'facebook.com', 'oculus.com', 'instagram.com'].some(d => u.hostname === d || u.hostname.endsWith(`.${d}`))) authWindow?.loadURL(target); return { action: 'deny' }; });
    authWindow.on('closed', () => { authWindow = null; if (!completed) reject(new Error('Sign-in was cancelled.')); });
    authWindow.loadURL(url).catch(() => { if (!completed) { completed = true; authWindow?.close(); reject(new Error('Could not load Meta sign-in.')); } });
  });
}

async function downloadGame(id, binaryId, dlcId) {
  const game = getGame(id);
  if (state.data.jobs.some(j => j.gameId === id && ['queued', 'downloading', 'installing', 'patching'].includes(j.status))) throw new Error('This game already has an active task.');
  const api = store();
  const job = { id: randomUUID(), gameId: id, name: game.name, status: 'queued', completed: 0, total: 0, binaryId, dlcId, stage: 'Checking Quest build' };
  state.data.jobs.unshift(job); state.data.jobs = state.data.jobs.slice(0, 50);
  const controller = new AbortController(); controllers.set(job.id, controller); await persist();
  (async () => {
    try {
      const plan = dlcId ? { package: game.package, binaryId: game.binaryId, version: game.version, files: [] } : await api.plan(id, binaryId);
      if (dlcId) {
        if (!game.package) throw new Error('Download the base game before its add-ons.');
        const dlc = (await api.dlc(id)).find(d => d.id === dlcId);
        if (!dlc?.owned) throw new Error('Meta did not confirm ownership of this add-on.');
        if (!dlc.files.length) throw new Error('This add-on has no separately downloadable files; it may be included in the base game.');
        plan.files = dlc.files;
      }
      const target = path.join(state.data.settings.downloadDir, appId(id), appId(plan.binaryId));
      for (const file of plan.files) safeName(file.name);
      await checkSpace(target, plan.files.reduce((n, f) => n + Number(f.size || 0), 0));
      job.total = plan.files.reduce((n, f) => n + Number(f.size || 0), 0); job.status = 'downloading';
      await persist();
      const files = [];
      let completed = 0, lastUpdate = 0;
      for (const file of plan.files) {
        controller.signal.throwIfAborted();
        job.stage = file.name;
        const destination = path.join(target, safeName(file.name));
        const result = await downloadFile({ url: await api.downloadUrl(file), destination, size: Number(file.size || 0), signal: controller.signal,
          progress: (bytes, total) => { job.completed = completed + bytes; if (!job.total) job.currentTotal = total;
            if (Date.now() - lastUpdate > 200) { changed(); lastUpdate = Date.now(); } } });
        completed += result.bytes;
        files.push({ name: file.name, path: destination, kind: file.kind, size: result.bytes, sha256: result.sha256 });
      }
      if (!dlcId) {
        const apk = files.find(f => f.kind === 'apk');
        const metadata = await runtime.inspect(apk.path);
        if (metadata.package !== plan.package) throw new Error('Downloaded APK package does not match the selected build.');
        Object.assign(game, { package: metadata.package, activity: metadata.activity, apk: apk.path, patched: metadata.patched,
          version: plan.version, binaryId: plan.binaryId, files, downloaded: true });
      } else {
        game.files = [...(game.files || []).filter(f => !files.some(n => n.name === f.name)), ...files];
      }
      state.put(game);
      job.completed = completed; job.total = completed; job.status = 'complete'; job.stage = dlcId ? 'Add-on downloaded' : 'Ready to install';
    } catch (error) { job.status = controller.signal.aborted ? 'cancelled' : 'failed'; job.error = message(error); }
    finally { controllers.delete(job.id); await persist(); }
  })();
  return job.id;
}

async function bootstrap() {
state = new State(app.getPath('userData')); await state.load();
state.data.settings = { sdk: path.join(process.env.LOCALAPPDATA || '', 'Android/Sdk'), avd: 'axrb-games-api34', port: 5580,
  memoryMB: 8192, downloadDir: path.join(app.getPath('downloads'), 'AXRB'), ovrportCli: '',
  guestClock: await exists(path.join(root, 'build-whpx-clock/Release/axrb_clock_launcher.exe')) ? 'TscCorrected' : 'Default', ...state.data.settings };
runtime = new Runtime(root, state.data.settings);
try { if (safeStorage.isEncryptionAvailable()) token = safeStorage.decryptString(await fs.readFile(path.join(state.directory, 'meta-session.bin'))); } catch {}
window = new BrowserWindow({ width: 1320, height: 880, minWidth: 920, minHeight: 640, title: 'AXRB', backgroundColor: '#141414',
  autoHideMenuBar: true, webPreferences: { preload: path.join(directory, 'preload.cjs'), sandbox: true, contextIsolation: true, nodeIntegration: false } });
window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
window.webContents.on('will-navigate', event => event.preventDefault());
window.webContents.session.setPermissionRequestHandler((_wc, _permission, callback) => callback(false));
handler('state', () => publicState());
handler('login', async () => { await login(); return syncMeta(); });
handler('logout', async () => { for (const controller of controllers.values()) controller.abort(); token = ''; account = ''; await fs.rm(path.join(state.directory, 'meta-session.bin'), { force: true }); changed(); });
handler('sync', async () => { const online = await syncInstalled(); const result = token ? await syncMeta() : null; return { online, meta: result }; });
handler('search', async text => { const games = await new QuestStore(token).search(text); for (const game of games) searchResults.set(game.id, game); return games; });
handler('add', async id => { const game = searchResults.get(appId(id)); if (!game) throw new Error('Search for this app again.'); const existing = state.data.games.find(g => g.id === game.id); if (!existing) state.put(game); await persist(); return game.id; });
handler('lookup', async input => { const game = await store().details(appId(input)); state.put(game); await persist(); return game.id; });
handler('builds', id => store().builds(appId(id)).then(items => items.map(b => ({ id: String(b.id), version: b.version, code: b.version_code ?? b.versionCode }))));
handler('download', (id, binaryId) => downloadGame(appId(id), binaryId));
handler('dlc', id => store().dlc(appId(id)).then(items => items.map(({ files, ...item }) => ({ ...item, fileCount: files.length, bytes: files.reduce((n, f) => n + f.size, 0) }))));
handler('downloadDlc', (id, dlcId) => downloadGame(appId(id), null, appId(dlcId)));
handler('cancel', id => { controllers.get(id)?.abort(); });
handler('retry', id => { const job = state.data.jobs.find(j => j.id === id); if (!job || !['failed', 'interrupted', 'cancelled'].includes(job.status)) throw new Error('This task cannot be retried.'); return downloadGame(job.gameId, job.binaryId, job.dlcId); });
handler('import', () => exclusive(async () => {
  const result = await dialog.showOpenDialog(window, { title: 'Import an Android game', filters: [{ name: 'Android APK', extensions: ['apk'] }], properties: ['openFile'] });
  if (result.canceled) return;
  const game = await runtime.inspect(result.filePaths[0]);
  const existing = state.data.games.find(g => g.package === game.package);
  state.put({ ...game, id: existing?.id || game.id, source: existing?.source || game.source, downloaded: true });
  await persist(); return game.package;
}));
handler('importAssets', id => exclusive(async () => {
  const game = getGame(id);
  const result = await dialog.showOpenDialog(window, { title: 'Add expansion files / DLC assets', properties: ['openFile', 'multiSelections'] });
  if (result.canceled) return;
  const files = [];
  for (const file of result.filePaths) files.push({ path: file, name: safeName(path.basename(file)), kind: file.endsWith('.obb') ? 'obb' : 'asset', size: (await fs.stat(file)).size });
  game.files = [...(game.files || []).filter(f => !files.some(n => n.name === f.name)), ...files]; await persist();
}));
handler('install', id => exclusive(async () => {
  const game = getGame(id);
  // Verify downloaded artifacts before any installation; imported APKs remain user-managed.
  for (const file of game.files || []) if (file.sha256) {
    const hash = createHash('sha256'), handle = await fs.open(file.path);
    try { for await (const chunk of handle.createReadStream()) hash.update(chunk); } finally { await handle.close(); }
    if (hash.digest('hex') !== file.sha256) throw new Error(`${file.name} changed since download. Download it again.`);
  }
  const job = { id: randomUUID(), gameId: id, name: game.name, status: 'installing', stage: 'Preparing install' }; state.data.jobs.unshift(job); await persist();
  try { await runtime.install(game, stage => { job.stage = stage; changed(); }); game.installed = true; job.status = 'complete'; job.stage = 'Installed'; }
  catch (error) { job.status = 'failed'; job.error = message(error); throw error; }
  finally { await persist(); }
}));
handler('patch', id => exclusive(async () => {
  const game = getGame(id), cli = state.data.settings.ovrportCli;
  if (!cli || !await exists(cli)) throw new Error('Choose the ovrport CLI executable or JAR in Settings first.');
  if (!game.apk) throw new Error('Download or import the APK first.');
  const output = path.join(path.dirname(game.apk), `${path.basename(game.apk, '.apk')}-axrb.apk`);
  const args = ['patch', `--input=${game.apk}`, `--output=${output}`];
  await run(cli.endsWith('.jar') ? 'java' : cli, cli.endsWith('.jar') ? ['-jar', cli, ...args] : args, { timeout: 20 * 60 * 1000 });
  const metadata = await runtime.inspect(output);
  if (metadata.package !== game.package) throw new Error('Patched APK changed its package name; import it separately.');
  game.apk = output; game.patched = true; await persist();
}));
handler('play', async id => { const game = getGame(id); if (busy) throw new Error('Wait for installation to finish.');
  if (!game.installed) throw new Error('Install the game first.');
  runtime.launch(game, async (code, tail) => {
    if (code) {
      const error = message(new Error(tail || `Game launcher exited with code ${code}.`));
      state.data.jobs.unshift({ id: randomUUID(), gameId: id, name: game.name, status: 'failed', stage: 'Launch', error });
      if (window && !window.isDestroyed()) window.webContents.send('axrb:launch-error', `${game.name}: ${error}`);
    }
    await persist();
  });
  game.lastPlayed = new Date().toISOString(); await persist(); });
handler('stop', () => runtime.stop());
handler('settings', async values => {
  const allowed = ['sdk', 'avd', 'port', 'memoryMB', 'downloadDir', 'ovrportCli'];
  if (!values || typeof values !== 'object') throw new Error('Invalid settings.');
  if (busy || controllers.size || runtime.child) throw new Error('Finish current tasks before changing runtime settings.');
  const settings = { ...state.data.settings };
  for (const key of allowed) if (values[key] !== undefined) settings[key] = values[key];
  if (!/^[A-Za-z0-9_-]+$/.test(settings.avd) || !Number.isInteger(settings.port) || settings.port < 5554 || settings.port > 5682 || settings.port % 2 ||
    !Number.isInteger(settings.memoryMB) || settings.memoryMB < 2048 || settings.memoryMB > 16384) throw new Error('Check the Android AVD, even-numbered port, and memory settings.');
  for (const key of ['sdk', 'downloadDir']) if (typeof settings[key] !== 'string' || !path.isAbsolute(settings[key])) throw new Error('Select absolute Windows paths.');
  state.data.settings = settings; runtime.settings = settings; await persist();
});
handler('chooseFolder', async () => { const choice = await dialog.showOpenDialog(window, { properties: ['openDirectory', 'createDirectory'] }); return choice.canceled ? null : choice.filePaths[0]; });
handler('chooseCli', async () => { const choice = await dialog.showOpenDialog(window, { properties: ['openFile'], filters: [{ name: 'ovrport CLI', extensions: ['exe', 'jar'] }] }); return choice.canceled ? null : choice.filePaths[0]; });
handler('openFolder', async id => { const game = getGame(id); const target = game.apk ? path.dirname(game.apk) : state.data.settings.downloadDir; await fs.mkdir(target, { recursive: true }); const error = await shell.openPath(target); if (error) throw new Error(error); });
handler('openStore', async id => shell.openExternal(id ? `https://www.meta.com/experiences/${appId(id)}/` : 'https://www.meta.com/experiences/'));
const uiErrors = [];
if (smoke) window.webContents.on('console-message', details => { if (details.level === 'error') uiErrors.push(details.message); });
await window.loadFile(uiPath);
window.show();
window.focus();
if (smoke) {
  const { uiSmoke } = await import('./tests/ui-smoke.mjs');
  await uiSmoke(window, path.join(root, 'build-launcher-smoke'), publicState, uiErrors);
  app.quit();
} else { syncInstalled().catch(() => {}); refreshArtwork().catch(() => {}); }
app.on('window-all-closed', () => { for (const controller of controllers.values()) controller.abort(); app.quit(); });
}
app.whenReady().then(bootstrap).catch(error => { console.error(message(error)); if (!smoke) dialog.showErrorBox('AXRB could not start', message(error)); app.exit(1); });
