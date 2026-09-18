import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import { installFiles } from './game-files.mjs';

export function run(executable, args, { timeout = 120000, onOutput = () => {}, signal, requireCompleteOutput = false, rejectStderr = false } = {}) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) { reject(new Error('Cancelled')); return; }
    const child = spawn(executable, args, { windowsHide: true, shell: false });
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    let output = '', errors = '', settled = false, truncated = false;
    const timer = setTimeout(() => { child.kill(); finish(new Error('Operation timed out. Check the Android runtime and try again.')); }, timeout);
    const abort = () => { child.kill(); };
    signal?.addEventListener('abort', abort, { once: true });
    function finish(error) { if (settled) return; settled = true; clearTimeout(timer); signal?.removeEventListener('abort', abort); error ? reject(error) : resolve(output); }
    child.stdout.on('data', b => { const next = output + b.toString(); truncated ||= next.length > 8 * 1024 * 1024; output = next.slice(-8 * 1024 * 1024); onOutput(b.toString()); });
    child.stderr.on('data', b => { errors = (errors + b.toString()).slice(-16384); onOutput(b.toString()); });
    child.on('error', e => finish(new Error(`Could not start ${path.basename(executable)}: ${e.code || 'unknown error'}`)));
    child.on('close', code => finish(signal?.aborted ? new Error('Cancelled') : requireCompleteOutput && truncated ? new Error('Device file list exceeds the supported size; no incomplete import was saved.') : code === 0 && !(rejectStderr && errors.trim()) ? null : new Error((errors || output || `Process exited with code ${code}`).slice(-3000))));
  });
}
const psLiteral = value => `'${String(value).replaceAll("'", "''")}'`;
export function powershellArgs(script, parameters) {
  const invocation = `$ProgressPreference = 'SilentlyContinue'; [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new(); try { & ${psLiteral(script)} ${Object.entries(parameters).map(([key, value]) => {
    if (!/^[a-zA-Z]+$/.test(key)) throw new Error('Invalid PowerShell parameter.');
    return typeof value === 'boolean' ? `-${key}:$${value}` : `-${key} ${psLiteral(value)}`;
  }).join(' ')}; if (-not $?) { exit 1 } } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }`;
  return ['-NoProfile', '-NonInteractive', '-OutputFormat', 'Text', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', Buffer.from(invocation, 'utf16le').toString('base64')];
}
export function windowsFeaturesCommand(windowsDir = process.env.WINDIR || 'C:\\Windows') {
  const executable = path.join(windowsDir, 'System32', 'optionalfeatures.exe');
  if (!path.isAbsolute(executable) || /[\r\n\x00]/.test(executable)) throw new Error('Windows Features path is invalid.');
  const literal = executable.replaceAll("'", "''");
  const script = `$exe = '${literal}'; if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Windows Features is unavailable on this Windows installation.' }; Start-Process -LiteralPath $exe -WindowStyle Normal`;
  return ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')];
}
export async function openWindowsFeatures(execute = run) {
  return execute('powershell.exe', windowsFeaturesCommand(), { timeout: 15000 });
}
export function validPackage(value) {
  if (!/^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+$/.test(value || '')) throw new Error('Invalid Android package name.');
  return value;
}
export class Runtime {
  constructor(root, settings) { this.root = root; this.settings = settings; this.child = null; this.game = null; }
  adb(args, options) { return run(path.join(this.settings.sdk, 'platform-tools/adb.exe'), ['-s', `emulator-${this.settings.port}`, ...args], options); }
  async online() { try { return (await this.adb(['get-state'], { timeout: 2500 })).trim() === 'device'; } catch { return false; } }
  async ensure({ onOutput = () => {} } = {}) {
    if (await this.online()) {
      const name = (await this.adb(['emu', 'avd', 'name'])).split(/\r?\n/)[0].trim();
      if (name !== this.settings.avd) throw new Error(`Android port is occupied by ${name}. Select that AVD or stop it first.`);
      const cores = Number((await this.adb(['shell', 'getconf', '_NPROCESSORS_ONLN'])).trim());
      if (cores !== (this.settings.cpuCores ?? 4)) throw new Error('Restart Android to apply the selected vCPU count.');
      return;
    }
    await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/emulator/windows_android_emulator.ps1'), {
      Action: 'Start', Avd: this.settings.avd, Port: this.settings.port, Sdk: this.settings.sdk,
      ApiLevel: 36, Abi: 'arm64-v8a', MemoryMB: this.settings.memoryMB, CpuCores: this.settings.cpuCores ?? 4, GuestClock: this.settings.guestClock || 'Default', GpuSharing: true
    }), { timeout: (this.settings.guestClock || 'Default') === 'TscCorrected' ? 17 * 60 * 1000 : 10 * 60 * 1000, onOutput });
  }
  async inspect(apk, { allowSplit = false } = {}) {
    const data = JSON.parse(await run('python', [path.join(this.root, 'launcher/inspect_apk.py'), '--apk', apk, '--sdk', this.settings.sdk, ...(allowSplit ? ['--allow-split'] : [])]));
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
    const metadata = JSON.parse(await run('python', [path.join(this.root, 'scripts/run/android_app_label.py'), '--sdk', this.settings.sdk,
      '--serial', `emulator-${this.settings.port}`, '--package', packageName, '--icon-output', imagePath]));
    return { id: `local:${packageName}`, package: packageName, activity, name: metadata.label, source: 'installed', installed: true,
      image: metadata.icon ? `data:image/png;base64,${(await fs.readFile(metadata.icon)).toString('base64')}` : '' };
  }
  async install(game, update = () => {}) {
    validPackage(game.package);
    if (this.child) throw new Error('Close the running game before installing.');
    if (!game.apk) throw new Error('Import or download an APK first.');
    update('Starting Android'); await this.ensure();
    await installFiles(game, (args, options) => this.adb(args, options), update);
  }
  async uninstall(game, update = () => {}) {
    validPackage(game.package);
    if (game.package.startsWith('com.axrb.')) throw new Error('The AXRB runtime cannot be uninstalled here.');
    if (this.child) throw new Error('Close the running game before uninstalling.');
    update('Starting Android'); await this.ensure();
    const installed = await this.installed();
    if (!installed) throw new Error('Android disconnected. Try again.');
    if (!installed.has(game.package)) throw new Error('Game is not installed. Refresh your library.');
    update('Uninstalling');
    const result = await this.adb(['uninstall', game.package], { timeout: 240000 });
    if (!/^Success\s*$/m.test(result)) throw new Error(result.trim() || 'Android did not confirm the uninstall.');
  }
  launch(game, onExit) {
    if (this.child) throw new Error('A game is already running.');
    validPackage(game.package);
    if (!/^[A-Za-z0-9_./]+$/.test(game.activity || '') || game.activity.split('/')[0] !== game.package) throw new Error('Invalid launch activity. Refresh installed games.');
    this.fpsHudEvent = `Local\\AXRB.FpsHud.${randomUUID().replaceAll('-', '')}`;
    const args = powershellArgs(path.join(this.root, 'scripts/run/run_windows_game.ps1'), { Avd: this.settings.avd, Port: this.settings.port,
      Sdk: this.settings.sdk, MemoryMB: this.settings.memoryMB, CpuCores: this.settings.cpuCores ?? 4, Package: game.package, Activity: game.activity, GameName: game.name, FpsHud: this.settings.fpsHud === true, FpsHudEventName: this.fpsHudEvent,
      ...(this.settings.managedDirectory ? { RuntimeApk: path.join(this.root, 'out/android/runtime-arm64-v8a/axrb-openxr-runtime-debug.apk') } : {}) });
    // Windows PowerShell can exit successfully without executing its command
    // when CREATE_NEW_PROCESS_GROUP/detached is combined with no console.
    const child = spawn('powershell.exe', args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    this.child = child; this.game = game.id;
    let tail = '';
    child.stdout.on('data', b => { tail = (tail + b).slice(-4000); });
    child.stderr.on('data', b => { tail = (tail + b).slice(-4000); });
    let finished = false;
    const end = (code, error) => { if (finished) return; finished = true; this.child = null; this.game = null; this.fpsHudEvent = null; onExit(code, error || tail); };
    child.on('error', e => end(1, e.message)); child.on('exit', code => end(code)); child.unref();
  }
  async setFpsHud(enabled) {
    if (typeof enabled !== 'boolean') throw new Error('Invalid FPS HUD setting.');
    if (this.child && this.fpsHudEvent) {
      await run('powershell.exe', powershellArgs(path.join(this.root, 'scripts/run/fps_hud.ps1'), {
        EventName: this.fpsHudEvent, Enabled: enabled ? 1 : 0
      }), { timeout: 5000 });
    }
    this.settings.fpsHud = enabled;
  }
  async stop() {
    if (!this.child) return;
    const pid = this.child.pid;
    const script = `$p = Get-CimInstance Win32_Process -Filter "Name='axrb-host-bridge.exe'" | Where-Object ParentProcessId -eq ${Number(pid)}; foreach ($h in $p) { $null = (Get-Process -Id $h.ProcessId).CloseMainWindow() }`;
    await run('powershell.exe', ['-NoProfile', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')]);
  }
}
