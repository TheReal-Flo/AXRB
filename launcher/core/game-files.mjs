import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { archivePath, extractZip, zipSize } from './archive.mjs';
import { checkSpace, safeName } from './download.mjs';

export const shellQuote = value => `'${String(value).replaceAll("'", "'\\''")}'`;

export function assetDestination(packageName, relative) {
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(packageName)) throw new Error('Invalid package');
  if (typeof relative !== 'string') throw new Error('Missing asset path');
  archivePath(path.resolve('asset-check'), relative);
  const parts = relative.split('/');
  if (parts.some(p => !p || p === '.')) throw new Error('Invalid asset path');
  if (parts[0] !== 'Android' || !['obb', 'data'].includes(parts[1]) || parts[2] !== packageName || parts.length < 4) throw new Error('Asset path does not belong to this game.');
  return `/sdcard/${relative}`;
}

export function classifyAsset(name, packageName) {
  const parts = name.split('/');
  // Allow one enclosing game folder or sdcard prefix, but retain the Android layout.
  const android = parts.indexOf('Android');
  if (android >= 0) {
    const destination = parts.slice(android).join('/');
    assetDestination(packageName, destination);
    return destination;
  }
  const area = parts.findIndex(p => p === 'obb' || p === 'data');
  if (area >= 0) {
    const destination = `Android/${parts.slice(area).join('/')}`;
    assetDestination(packageName, destination); return destination;
  }
  const base = parts.at(-1);
  if (/\.obb$/i.test(base)) {
    if (!new RegExp(`^(?:main|patch)\\.\\d+\\.${packageName.replaceAll('.', '\\.')}\\.obb$`, 'i').test(base)) throw new Error(`OBB does not match ${packageName}: ${base}`);
    return `Android/obb/${packageName}/${safeName(base)}`;
  }
  throw new Error(`Cannot determine where ${name} belongs. Place assets under Android/obb/${packageName} or Android/data/${packageName}.`);
}

export async function walkFiles(directory, prefix = '') {
  const files = [];
  for (const entry of await fs.readdir(path.join(directory, prefix), { withFileTypes: true })) {
    const name = prefix ? `${prefix}/${entry.name}` : entry.name;
    if (entry.isSymbolicLink()) throw new Error('Linked files are not supported.');
    if (entry.isDirectory()) files.push(...await walkFiles(directory, name));
    else if (entry.isFile()) files.push(name);
  }
  return files;
}

export async function fileRecord(file, name, kind, destination, signal) {
  const hash = createHash('sha256'), handle = await fs.open(file);
  try { for await (const chunk of handle.createReadStream()) { signal?.throwIfAborted(); hash.update(chunk); } }
  finally { await handle.close(); }
  return { path: file, name, kind, ...(destination ? { destination } : {}), size: (await fs.stat(file)).size, sha256: hash.digest('hex') };
}

export async function inspectGameFolder(directory, inspect, { signal, update = () => {} } = {}) {
  const names = await walkFiles(directory), apks = [];
  for (const name of names.filter(n => /\.apk$/i.test(n) && !n.split('/').some(p => ['Android', 'obb', 'data'].includes(p)))) {
    signal?.throwIfAborted(); update(`Reading ${name}`);
    apks.push({ name, metadata: await inspect(path.join(directory, name), { allowSplit: true }) });
  }
  const bases = apks.filter(a => !a.metadata.split);
  if (bases.length !== 1) throw new Error('ZIP must contain exactly one base APK, plus any matching split APKs.');
  const game = bases[0].metadata, splitNames = new Set();
  for (const apk of apks) {
    if (apk.metadata.package !== game.package || apk.metadata.versionCode !== game.versionCode || splitNames.has(apk.metadata.split)) throw new Error('APK splits must belong to the same game and version.');
    splitNames.add(apk.metadata.split);
  }
  const files = [], destinations = new Set();
  for (const name of names) {
    signal?.throwIfAborted();
    if (name.startsWith('__MACOSX/') || name.endsWith('/.DS_Store') || name === '.DS_Store') continue;
    const apk = apks.find(a => a.name === name);
    const destination = apk ? undefined : classifyAsset(name, game.package);
    if (destination && destinations.has(destination.toLowerCase())) throw new Error('Multiple assets target the same file.');
    if (destination) destinations.add(destination.toLowerCase());
    update(`Verifying ${name}`);
    files.push(await fileRecord(path.join(directory, name), name, apk ? (apk.metadata.split ? 'split' : 'apk') : 'asset', destination, signal));
  }
  return { ...game, apk: path.join(directory, bases[0].name), files, downloaded: true };
}

export async function importGameZip(file, downloadDir, inspect, { signal, update = () => {}, progress = () => {}, reserve } = {}) {
  const directory = path.join(downloadDir, 'imports', randomUUID());
  try {
    update('Checking ZIP');
    const size = await zipSize(file, directory, { signal });
    await checkSpace(directory, size, reserve === undefined ? {} : { reserve });
    update('Extracting ZIP');
    await extractZip(file, directory, { signal, limit: 256 * 1024 ** 3, progress });
    return await inspectGameFolder(directory, inspect, { signal, update });
  } catch (error) { await fs.rm(directory, { recursive: true, force: true }); throw error; }
}

export async function installFiles(game, adb, update = () => {}, signal) {
  const splits = (game.files || []).filter(f => f.kind === 'split');
  const assets = (game.files || []).filter(f => !['apk', 'split'].includes(f.kind)).map(file => ({ ...file,
    remote: assetDestination(game.package, file.destination || `Android/obb/${game.package}/${safeName(file.name)}`) }));
  if (!game.apk) throw new Error('Import or download an APK first.');
  const apkPaths = [game.apk, ...splits.map(f => f.path)];
  let bytes = 0;
  for (const file of [...apkPaths, ...assets.map(f => f.path)]) bytes += (await fs.stat(file)).size;
  const disk = await adb(['shell', 'df', '-k', '/data'], { signal });
  const available = disk.trim().split(/\r?\n/).at(-1)?.trim().split(/\s+/)[3];
  if (!/^\d+$/.test(available || '')) throw new Error('Could not check Android free space.');
  if (Number(available) * 1024 < bytes + 1024 ** 3) throw new Error(`Android needs at least ${(bytes / 1024 ** 3 + 1).toFixed(1)} GB free for this installation. Free space and retry.`);
  signal?.throwIfAborted(); update('Installing APK');
  await adb([splits.length ? 'install-multiple' : 'install', '--no-incremental', '--force-queryable', '-r', ...apkPaths], { timeout: 30 * 60 * 1000, signal });
  for (const file of assets) {
    signal?.throwIfAborted(); update(`Copying ${file.name}`);
    await adb(['shell', `mkdir -p ${shellQuote(path.posix.dirname(file.remote))}`], { signal });
    await adb(['push', file.path, file.remote], { timeout: 60 * 60 * 1000, signal, onOutput: text => update(`Copying ${file.name}: ${text.trim().slice(-120)}`) });
  }
  await adb(['shell', 'sync'], { signal });
}
