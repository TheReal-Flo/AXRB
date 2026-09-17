import { spawn } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';

export function run(executable, args, { timeout = 120000, onOutput = () => {} } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, { windowsHide: true, shell: false });
    let output = '', errors = '', settled = false;
    const timer = setTimeout(() => { child.kill(); finish(new Error('Operation timed out. Check the Android runtime and try again.')); }, timeout);
    function finish(error) { if (settled) return; settled = true; clearTimeout(timer); error ? reject(error) : resolve(output); }
    child.stdout.on('data', b => { output = (output + b.toString()).slice(-8 * 1024 * 1024); onOutput(b.toString()); });
    child.stderr.on('data', b => { errors = (errors + b.toString()).slice(-16384); onOutput(b.toString()); });
    child.on('error', e => finish(new Error(`Could not start ${path.basename(executable)}: ${e.code || 'unknown error'}`)));
    child.on('exit', code => finish(code === 0 ? null : new Error((errors || output || `Process exited with code ${code}`).slice(-3000))));
  });
}
const psLiteral = value => `'${String(value).replaceAll("'", "''")}'`;
export function powershellArgs(script, parameters) {
  const invocation = `& ${psLiteral(script)} ${Object.entries(parameters).map(([key, value]) => {
    if (!/^[a-zA-Z]+$/.test(key)) throw new Error('Invalid PowerShell parameter.');
    return value === true ? `-${key}:$true` : `-${key} ${psLiteral(value)}`;
  }).join(' ')}; if (-not $?) { exit 1 }`;
  return ['-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', Buffer.from(invocation, 'utf16le').toString('base64')];
}
export function validPackage(value) {
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(value || '')) throw new Error('Invalid Android package name.');
  return value;
}
export class Runtime {
  constructor(root, settings) { this.root = root; this.settings = settings; this.child = null; this.game = null; }
  adb(args, options) { return run(path.join(this.settings.sdk, 'platform-tools/adb.exe'), ['-s', `emulator-${this.settings.port}`, ...args], options); }
  async online() { try { return (await this.adb(['get-state'], { timeout: 2500 })).trim() === 'device'; } catch { return false; } }
  async ensure() {
    if (await this.online()) {
      const name = (await this.adb(['emu', 'avd', 'name'])).split(/\r?\n/)[0].trim();
      if (name !== this.settings.avd) throw new Error(`Android port is occupied by ${name}. Select that AVD or stop it first.`);
      return;
    }
    await run('powershell.exe', powershellArgs(path.join(this.root, 'tools/windows_android_emulator.ps1'), {
      Action: 'Start', Avd: this.settings.avd, Port: this.settings.port, Sdk: this.settings.sdk,
      Abi: 'arm64-v8a', MemoryMB: this.settings.memoryMB, GuestClock: this.settings.guestClock || 'Default', GpuSharing: true
    }), { timeout: 240000 });
  }
  async inspect(apk) {
    const data = JSON.parse(await run('python', [path.join(this.root, 'launcher/inspect_apk.py'), '--apk', apk, '--sdk', this.settings.sdk]));
    validPackage(data.package);
    return { ...data, apk: path.resolve(apk), source: 'local', id: `local:${data.package}` };
  }
  async installed() {
    if (!await this.online()) return null;
    return new Set((await this.adb(['shell', 'pm', 'list', 'packages', '-3'])).split(/\r?\n/).map(s => s.replace(/^package:/, '').trim()).filter(Boolean));
  }
  async importInstalled(packageName, imagePath) {
    validPackage(packageName);
    const activity = (await this.adb(['shell', 'cmd', 'package', 'resolve-activity', '--brief', packageName])).split(/\r?\n/).find(s => s.startsWith(`${packageName}/`));
    if (!activity) return null;
    const metadata = JSON.parse(await run('python', [path.join(this.root, 'tools/android_app_label.py'), '--sdk', this.settings.sdk,
      '--serial', `emulator-${this.settings.port}`, '--package', packageName, '--icon-output', imagePath]));
    return { id: `local:${packageName}`, package: packageName, activity, name: metadata.label, source: 'installed', installed: true,
      image: metadata.icon ? `data:image/png;base64,${(await fs.readFile(metadata.icon)).toString('base64')}` : '' };
  }
  async install(game, update = () => {}) {
    validPackage(game.package);
    if (this.child) throw new Error('Close the running game before installing.');
    if (!game.apk) throw new Error('Import or download an APK first.');
    update('Starting Android'); await this.ensure();
    update('Installing APK');
    await this.adb(['install', '--no-incremental', '--force-queryable', '-r', game.apk], { timeout: 240000 });
    for (const file of game.files || []) {
      if (file.kind === 'apk') continue;
      update(`Copying ${file.name}`);
      // Preserve the filename supplied by Meta for expansion/asset delivery.
      const directory = `/sdcard/Android/obb/${game.package}`;
      await this.adb(['shell', 'mkdir', '-p', directory]);
      // shell mkdir only contains the validated package, not server filenames.
      await this.adb(['push', file.path, `${directory}/${file.name}`], { timeout: 30 * 60 * 1000 });
    }
    await this.adb(['shell', 'sync']);
  }
  launch(game, onExit) {
    if (this.child) throw new Error('A game is already running.');
    validPackage(game.package);
    if (!/^[A-Za-z0-9_./]+$/.test(game.activity || '') || game.activity.split('/')[0] !== game.package) throw new Error('Invalid launch activity. Refresh installed games.');
    const args = powershellArgs(path.join(this.root, 'tools/run_windows_game.ps1'), { Avd: this.settings.avd, Port: this.settings.port,
      Sdk: this.settings.sdk, MemoryMB: this.settings.memoryMB, Package: game.package, Activity: game.activity, GameName: game.name });
    // Windows PowerShell can exit successfully without executing its command
    // when CREATE_NEW_PROCESS_GROUP/detached is combined with no console.
    const child = spawn('powershell.exe', args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    this.child = child; this.game = game.id;
    let tail = '';
    child.stdout.on('data', b => { tail = (tail + b).slice(-4000); });
    child.stderr.on('data', b => { tail = (tail + b).slice(-4000); });
    let finished = false;
    const end = (code, error) => { if (finished) return; finished = true; this.child = null; this.game = null; onExit(code, error || tail); };
    child.on('error', e => end(1, e.message)); child.on('exit', code => end(code)); child.unref();
  }
  async stop() {
    if (!this.child) return;
    const pid = this.child.pid;
    const script = `$p = Get-CimInstance Win32_Process -Filter "Name='axrb-host-bridge.exe'" | Where-Object ParentProcessId -eq ${Number(pid)}; foreach ($h in $p) { $null = (Get-Process -Id $h.ProcessId).CloseMainWindow() }`;
    await run('powershell.exe', ['-NoProfile', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')]);
  }
}
