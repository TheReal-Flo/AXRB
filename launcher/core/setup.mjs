import fs from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { downloadFile, checkSpace } from './download.mjs';
import { extractZip } from './archive.mjs';
import { run, powershellArgs } from './runtime.mjs';

export const exists = file => fs.access(file).then(() => true, () => false);
export const hardwareRequirementsMet = hardware => Boolean(hardware?.supportedGpu && hardware.x64 && hardware.memoryGB >= 12);
export const supportedSystem = hardware => Boolean(hardware?.hypervisor && hardwareRequirementsMet(hardware));
export function setupPhase(hardware, { ready = false, debug = false } = {}) {
  if (!debug && !hardwareRequirementsMet(hardware)) return 'unsupported';
  if (!hardware?.hypervisor) return 'hypervisor';
  return ready ? 'ready' : 'install';
}
export function officialDownload(value) {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.hostname !== 'dl.google.com' || url.port || url.username || url.password || !url.pathname.startsWith('/android/repository/')) throw new Error('Untrusted runtime download URL');
  return url;
}
export async function verify(file, component) {
  const hash = createHash(component.sha256 ? 'sha256' : 'sha1');
  try {
    if ((await fs.stat(file)).size !== component.size) return false;
    for await (const chunk of createReadStream(file)) hash.update(chunk);
    return hash.digest('hex') === (component.sha256 || component.sha1);
  } catch (error) { if (error.code === 'ENOENT') return false; throw error; }
}
export function avdConfig(image, settings) {
  return Object.entries({ 'avd.ini.encoding': 'UTF-8', 'AvdId': settings.avd, 'avd.ini.displayname': 'AXRB',
    'abi.type': 'x86_64', 'hw.cpu.arch': 'x86_64', 'hw.cpu.ncore': settings.cpuCores,
    'hw.ramSize': settings.memoryMB, 'hw.gpu.enabled': 'yes', 'hw.gpu.mode': 'host',
    'hw.audioOutput': 'yes', 'hw.audioInput': 'yes', 'hw.lcd.width': 1080, 'hw.lcd.height': 1920,
    'hw.lcd.density': 420, 'hw.keyboard': 'no', 'hw.mainKeys': 'no', 'hw.useext4': 'yes',
    'disk.dataPartition.size': `${settings.storageGB ?? 32}G`, 'disk.cachePartition.size': '66MB', 'vm.heapSize': 576,
    'image.sysdir.1': image + path.sep, 'tag.id': 'google_apis', target: 'android-36',
    'fastboot.forceColdBoot': 'no', 'fastboot.forceFastBoot': 'yes', 'showDeviceFrame': 'no',
    'runtime.network.speed': 'full', 'runtime.network.latency': 'none', 'PlayStore.enabled': 'no',
  }).map(([key, value]) => `${key}=${value}`).join('\n') + '\n';
}

export class Setup {
  constructor({ root, directory, runtime, components, save, changed, debug = false }) {
    Object.assign(this, { root, directory, runtime, components, save, changed, debug });
    this.status = { phase: 'checking', directory, storageGB: runtime.settings?.storageGB ?? 32, completed: 0, total: 0, active: false, startedAt: 0, logs: [], debug };
  }
  update(value) {
    Object.assign(this.status, value);
    if (Object.keys(value).every(k => ['completed', 'total'].includes(k)) && Date.now() - (this.lastProgress || 0) < 100) return;
    this.lastProgress = Date.now(); this.changed();
  }
  appendLog(text) {
    const lines = String(text || '').replaceAll('\r', '').split('\n').filter(Boolean);
    if (!lines.length) return;
    const logs = [...(this.status.logs || []), ...lines].slice(-120);
    const now = Date.now();
    if (now - (this.lastLogUpdate || 0) < 150 && logs.length < 120) {
      this.status.logs = logs;
      return;
    }
    this.lastLogUpdate = now;
    this.update({ logs });
  }
  async runtimeHash() { return createHash('sha256').update(await fs.readFile(path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk'))).digest('hex'); }
  environment() {
    process.env.AXRB_DATA_HOME = path.join(this.directory, 'output');
    process.env.ANDROID_AVD_HOME = path.join(this.directory, 'avd');
    process.env.ANDROID_USER_HOME = path.join(this.directory, 'android');
    // Keep AXRB's emulator transport away from Android Studio, Quest tools,
    // and other emulators that may own the default ADB server on 5037.
    process.env.ANDROID_ADB_SERVER_PORT = '5038';
    delete process.env.ADB_SERVER_SOCKET;
    process.env.ANDROID_HOME = this.runtime.settings.sdk;
    process.env.ANDROID_SDK_ROOT = this.runtime.settings.sdk;
  }
  async check() {
    if (this.status.active) return;
    this.update({ phase: 'checking', error: '' });
    try {
      const hardware = JSON.parse(await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/emulator/check_windows.ps1'), {})));
      const ready = await exists(path.join(this.directory, 'ready.json')) && JSON.parse(await fs.readFile(path.join(this.directory, 'ready.json'), 'utf8')).runtimeHash === await this.runtimeHash() && (await Promise.all(this.components.map(async c => {
        const destination = path.join(this.runtime.settings.sdk, c.destination);
        if (!await exists(path.join(destination, c.probe)) || !await exists(path.join(destination, '.axrb-component.json'))) return false;
        return JSON.parse(await fs.readFile(path.join(destination, '.axrb-component.json'), 'utf8')).digest === (c.sha256 || c.sha1);
      }))).every(Boolean)
        && await exists(path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd/config.ini`));
      this.update({ hardware, phase: setupPhase(hardware, { ready, debug: this.debug }) });
    } catch (error) { this.update({ phase: 'error', error: error.message }); }
  }
  async start({ directory, accepted, storageGB = 32 }) {
    if (this.status.active) throw new Error('Setup is already running.');
    if (accepted !== true) throw new Error('Accept the Android SDK license to download Android.');
    if (!this.debug && !hardwareRequirementsMet(this.status.hardware)) throw new Error('Resolve the system requirements first.');
    if (!this.status.hardware?.hypervisor) throw new Error('Enable the Windows hypervisor first.');
    if (typeof directory !== 'string' || !path.isAbsolute(directory) || /[\r\n]/.test(directory)) throw new Error('Choose an absolute installation folder.');
    if (!Number.isInteger(storageGB) || storageGB < 8 || storageGB > 256) throw new Error('Choose 8–256 GB of Android storage.');
    if (await exists(path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd/userdata-qemu.img`)) && storageGB !== (this.runtime.settings.storageGB ?? 32)) throw new Error('Setup cannot resize an existing Android disk. Keep its current storage size.');
    // Own a child directory only; never replace user-selected directories themselves.
    const selected = path.join(directory, 'AXRB Runtime');
    if (await exists(path.join(this.directory, 'ready.json')) && path.resolve(selected) !== path.resolve(this.directory)) throw new Error('Use the existing runtime folder to update this installation.');
    this.directory = selected;
    this.runtime.settings.sdk = path.join(this.directory, 'sdk');
    this.runtime.settings.avd = 'axrb-managed-api36';
    this.runtime.settings.storageGB = storageGB;
    this.environment();
    this.controller = new AbortController();
    this.update({ phase: 'download', directory: this.directory, active: true, startedAt: Date.now(), cancelling: false, error: '', completed: 0, total: 0, logs: [] });
    this.task = this.install().catch(error => this.update({ phase: this.controller.signal.aborted ? 'cancelled' : 'error', error: this.controller.signal.aborted ? '' : error.message }))
      .finally(() => { this.update({ active: false }); this.controller = null; });
  }
  cancel() { if (this.controller) { this.update({ cancelling: true }); this.controller.abort(); } }
  async install() {
    const signal = this.controller.signal, sdk = this.runtime.settings.sdk;
    const cache = path.join(this.directory, 'downloads');
    await fs.mkdir(cache, { recursive: true });
    if (await this.runtime.online()) throw new Error('Close the running Android emulator before setting up or updating its files.');
    const dataImage = path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd/userdata-qemu.img`);
    if (!await exists(dataImage)) {
      const remaining = (await Promise.all(this.components.map(async c => await exists(path.join(sdk, c.destination, c.probe)) ? 0 : c.id === 'image' ? 6.1 : c.id === 'emulator' ? 1.6 : 0.3))).reduce((a, b) => a + b, 0);
      const space = await fs.statfs(this.directory), availableGB = Number(space.bavail) * Number(space.bsize) / 1024 ** 3;
      const requiredGB = this.runtime.settings.storageGB * 1.2 + remaining + 5;
      if (availableGB < requiredGB) throw new Error(`Setup needs ${Math.ceil(requiredGB)} GB free here (${availableGB.toFixed(1)} GB available). Choose another drive or a smaller Android disk.`);
    }
    await fs.writeFile(path.join(this.directory, 'license-acceptance.json'), JSON.stringify({ license: 'android-sdk-license', acceptedAt: new Date().toISOString() }));
    await this.save(this.directory);
    for (const c of this.components) {
      signal.throwIfAborted();
      const destination = path.join(sdk, c.destination), receipt = path.join(destination, '.axrb-component.json');
      if (await exists(receipt) && await exists(path.join(destination, c.probe))) {
        const installed = JSON.parse(await fs.readFile(receipt, 'utf8'));
        if (installed.digest === (c.sha256 || c.sha1)) continue;
      }
      await checkSpace(this.directory, c.id === 'image' ? 9 * 1024 ** 3 : c.size * 4);
      const archive = path.join(cache, `${c.id}.zip`);
      this.update({ phase: 'download', component: c.name, completed: 0, total: c.size });
      if (!await verify(archive, c)) {
        await fs.rm(archive, { force: true });
        await downloadFile({ ...c, destination: archive, signal, validate: officialDownload, progress: (completed, total) => this.update({ completed, total }) });
      }
      this.update({ phase: 'verify' });
      if (!await verify(archive, c)) { await fs.rm(archive, { force: true }); throw new Error(`${c.name}: checksum mismatch. Retry the download.`); }
      const staging = path.join(cache, `${c.id}-extract`);
      await fs.rm(staging, { recursive: true, force: true });
      this.update({ phase: 'extract', completed: 0, total: 0 });
      try {
        await extractZip(archive, staging, { signal, progress: (completed, total) => this.update({ completed, total }) });
        const source = path.join(staging, c.prefix);
        if (!await exists(path.join(source, c.probe))) throw new Error(`${c.name}: expected files are missing.`);
        if (c.id === 'emulator') {
          const qemu = path.join(source, 'qemu/windows-x86_64/qemu-system-x86_64-headless.exe');
          const hash = createHash('sha256'); for await (const bytes of createReadStream(qemu)) hash.update(bytes);
          if (hash.digest('hex') !== 'dcec1cc23ac57ff04ec748cde7e42bfc713bf2ad532e49606a4a9332cfb94b56') throw new Error('Emulator is incompatible with the AXRB clock adapter.');
        }
        await fs.mkdir(path.dirname(destination), { recursive: true });
        await fs.rm(destination, { recursive: true, force: true });
        await fs.rename(source, destination);
        await fs.writeFile(receipt, JSON.stringify({ digest: c.sha256 || c.sha1 }));
      } finally { await fs.rm(staging, { recursive: true, force: true }); }
      await fs.rm(archive, { force: true });
    }
    signal.throwIfAborted();
    const avd = path.join(this.directory, 'avd', `${this.runtime.settings.avd}.avd`);
    await fs.mkdir(avd, { recursive: true });
    if (!await exists(dataImage) || !await exists(path.join(avd, 'config.ini'))) await fs.writeFile(path.join(avd, 'config.ini'), avdConfig(path.join(sdk, 'system-images/android-36/google_apis/x86_64'), this.runtime.settings));
    await fs.writeFile(avd.slice(0, -4) + '.ini', `avd.ini.encoding=UTF-8\npath=${avd}\ntarget=android-36\n`);
    await fs.mkdir(process.env.ANDROID_USER_HOME, { recursive: true });
    // A user-selected port can belong to another AVD. Never modify or stop it.
    if (await this.runtime.online()) throw new Error('The setup Android port is in use. Close that emulator and retry.');
    this.update({ phase: 'boot', component: 'Starting Android', completed: 0, total: 0, logs: [] });
    try {
      await this.runtime.ensure({ onOutput: text => this.appendLog(text) });
      this.appendLog('Android boot completed; verifying GPU and ABI.');
      signal.throwIfAborted();
      this.update({ component: 'Installing AXRB runtime' });
      await this.runtime.adb(['install', '--no-incremental', '--force-queryable', '-r', path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk')], { timeout: 240000 });
      if (!(await this.runtime.adb(['shell', 'pm', 'path', 'com.axrb.openxrruntime'])).includes('package:')) throw new Error('Android did not register the AXRB runtime. Retry setup.');
      await this.runtime.adb(['shell', 'sync']);
      this.appendLog('AXRB runtime installed and synchronized.');
      signal.throwIfAborted();
    } finally {
      const name = await this.runtime.adb(['emu', 'avd', 'name']).catch(() => '');
      if (name.split(/\r?\n/)[0].trim() === this.runtime.settings.avd) {
        await this.runtime.adb(['emu', 'kill']).catch(() => {});
        const lock = path.join(avd, 'hardware-qemu.ini.lock');
        for (let i = 0; i < 80 && await exists(lock); i++) await new Promise(resolve => setTimeout(resolve, 500));
        if (await exists(lock)) throw new Error('Android is still shutting down. Wait a moment and retry setup.');
      }
    }
    signal.throwIfAborted();
    await fs.writeFile(path.join(this.directory, 'ready.json'), JSON.stringify({ version: 1, runtimeHash: await this.runtimeHash(), completedAt: new Date().toISOString() }));
    this.update({ phase: 'ready', component: '', completed: 0, total: 0 });
  }
}
