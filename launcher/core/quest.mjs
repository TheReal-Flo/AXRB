import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { run, validPackage } from './runtime.mjs';
import { archivePath } from './archive.mjs';
import { checkSpace, safeName } from './download.mjs';
import { inspectGameFolder, shellQuote } from './game-files.mjs';

export function parseDevices(output) {
  return output.split(/\r?\n/).flatMap(line => {
    const m = line.match(/^(\S+)\s+(device|offline|unauthorized)\b(.*)$/);
    return m && !m[1].startsWith('emulator-') ? [{ serial: m[1], status: m[2], name: m[3].match(/\bmodel:(\S+)/)?.[1].replaceAll('_', ' ') || m[1] }] : [];
  });
}

export class Quest {
  constructor(settings, execute = run) { this.settings = settings; this.execute = execute; }
  adb(args, options) { return this.execute(path.join(this.settings.sdk, 'platform-tools/adb.exe'), args, options); }
  device(serial, args, options) {
    if (typeof serial !== 'string' || !/^[A-Za-z0-9._:[\]-]+$/.test(serial) || serial.startsWith('-') || serial.startsWith('emulator-')) throw new Error('Invalid Quest device.');
    return this.adb(['-s', serial, ...args], options);
  }
  async devices() {
    const devices = parseDevices(await this.adb(['devices', '-l'], { timeout: 10000 }));
    const quests = [];
    for (const device of devices) {
      if (device.status !== 'device') { quests.push(device); continue; }
      const [model, manufacturer] = await Promise.all([
        this.device(device.serial, ['shell', 'getprop', 'ro.product.model']),
        this.device(device.serial, ['shell', 'getprop', 'ro.product.manufacturer'])
      ]);
      if (/quest/i.test(model) && /oculus|meta/i.test(manufacturer)) quests.push({ ...device, name: model.trim() });
    }
    return quests;
  }
  async requireDevice(serial) {
    const device = (await this.devices()).find(d => d.serial === serial);
    if (!device) throw new Error('Connect your Quest by USB and enable developer mode.');
    if (device.status !== 'device') throw new Error(device.status === 'unauthorized' ? 'Accept the USB debugging prompt inside your Quest, then refresh.' : 'Quest is offline. Reconnect it and refresh.');
    return device;
  }
  async games(serial) {
    await this.requireDevice(serial);
    const packages = (await this.device(serial, ['shell', 'pm', 'list', 'packages', '-3'], { requireCompleteOutput: true })).split(/\r?\n/).filter(s => s.startsWith('package:')).map(s => validPackage(s.slice(8).trim()));
    const remote = `/data/local/tmp/axrb-quest-catalog-${randomUUID()}.jar`;
    const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-quest-catalog-'));
    let labels;
    try {
      const helper = path.join(temp, 'catalog.jar');
      await fs.copyFile(fileURLToPath(new URL('../assets/quest-catalog.jar', import.meta.url)), helper);
      await this.device(serial, ['push', helper, remote]);
      const output = await this.device(serial, ['shell', `CLASSPATH=${shellQuote(remote)} app_process /system/bin QuestCatalog`], { requireCompleteOutput: true });
      labels = JSON.parse(output.trim().split(/\r?\n/).find(line => line.startsWith('[')) || '[]');
    } catch {
      // Package IDs remain usable on Quest versions that restrict shell metadata.
      labels = [];
    } finally {
      await this.device(serial, ['shell', `rm -f ${shellQuote(remote)}`]).catch(() => {});
      await fs.rm(temp, { recursive: true, force: true });
    }
    return [...new Set(packages)].map(packageName => ({ package: packageName,
      name: labels.find(app => app.package === packageName)?.name || packageName })).sort((a, b) => a.name.localeCompare(b.name));
  }
  async apkPaths(serial, packageName, signal) {
    const output = await this.device(serial, ['shell', 'pm', 'path', validPackage(packageName)], { signal, requireCompleteOutput: true });
    const paths = output.split(/\r?\n/).filter(s => s.startsWith('package:')).map(s => s.slice(8).trim());
    if (!paths.length || paths.some(p => !p.startsWith('/') || !p.endsWith('.apk') || /[\r\n\x00]/.test(p))) throw new Error('Could not locate the installed APKs.');
    return paths.sort();
  }
  async pullGame(serial, packageName, downloadDir, inspect, { signal, update = () => {}, progress = () => {}, reserve } = {}) {
    validPackage(packageName); await this.requireDevice(serial);
    const directory = path.join(downloadDir, 'quest', packageName, randomUUID());
    try {
      update('Checking Quest files');
      const apks = await this.apkPaths(serial, packageName, signal), files = [];
      for (const remote of apks) {
        const size = Number((await this.device(serial, ['shell', `stat -c %s ${shellQuote(remote)}`], { signal })).trim());
        if (!Number.isSafeInteger(size) || size <= 0) throw new Error('Could not read APK size.');
        files.push({ remote, name: safeName(path.posix.basename(remote)), size });
      }
      for (const area of ['obb', 'data']) {
        const base = `/sdcard/Android/${area}/${packageName}`;
        // List the parent first: a permission failure must not look like an absent directory.
        const entries = await this.device(serial, ['shell', `ls -1 ${shellQuote(`/sdcard/Android/${area}`)}`], { signal, requireCompleteOutput: true });
        if (!entries.split(/\r?\n/).includes(packageName)) continue;
        const listing = await this.device(serial, ['shell', `find ${shellQuote(base)} -type f -exec stat -c '%s %n' {} \\;`], { signal, requireCompleteOutput: true, rejectStderr: true });
        for (const line of listing.split(/\r?\n/).filter(Boolean)) {
          const match = line.match(/^(\d+) (.+)$/);
          if (!match || !match[2].startsWith(`${base}/`)) throw new Error('Could not read all Quest assets.');
          const name = match[2].slice('/sdcard/'.length);
          archivePath(directory, name);
          files.push({ remote: match[2], name, size: Number(match[1]) });
        }
      }
      const total = files.reduce((n, f) => n + f.size, 0);
      if (!Number.isSafeInteger(total) || new Set(files.map(f => f.name.toLowerCase())).size !== files.length) throw new Error('Invalid Quest file list.');
      await checkSpace(directory, total, reserve === undefined ? {} : { reserve });
      let completed = 0;
      progress(0, total);
      for (const file of files) {
        signal?.throwIfAborted(); update(`Copying ${file.name}`);
        const target = archivePath(directory, file.name);
        await fs.mkdir(path.dirname(target), { recursive: true });
        await this.device(serial, ['pull', file.remote, target], { timeout: 60 * 60 * 1000, signal,
          onOutput: text => { const percent = text.match(/\[\s*(\d+)%\]/); if (percent) progress(completed + file.size * Math.min(100, Number(percent[1])) / 100, total); } });
        if ((await fs.stat(target)).size !== file.size) throw new Error(`Incomplete transfer: ${file.name}`);
        completed += file.size; progress(completed, total);
      }
      if (JSON.stringify(apks) !== JSON.stringify(await this.apkPaths(serial, packageName, signal))) throw new Error('The game updated during transfer. Retry after the update finishes.');
      const game = await inspectGameFolder(directory, inspect, { signal, update });
      if (game.package !== packageName) throw new Error('Quest APK identity changed during transfer.');
      return { ...game, source: 'quest', id: `local:${packageName}` };
    } catch (error) { await fs.rm(directory, { recursive: true, force: true }); throw error; }
  }
}
