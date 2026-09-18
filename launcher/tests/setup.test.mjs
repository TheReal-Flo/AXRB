import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { archivePath, extractZip } from '../core/archive.mjs';
import { Setup, verify, officialDownload, avdConfig, hardwareRequirementsMet, setupPhase, supportedSystem } from '../core/setup.mjs';

test('runtime download trust excludes local URLs, credentials and unexpected hosts', () => {
  assert.equal(officialDownload('https://dl.google.com/android/repository/a.zip').hostname, 'dl.google.com');
  for (const value of ['http://dl.google.com/android/repository/a.zip', 'https://dl.google.com.evil.test/android/repository/a.zip', 'https://user@dl.google.com/android/repository/a.zip', 'https://127.0.0.1/a', 'https://dl.google.com/other.zip']) assert.throws(() => officialDownload(value));
});
test('archive paths cannot escape extraction or address alternate streams', () => {
  const root = path.resolve('scratch');
  for (const name of ['../outside', '/absolute', 'C:/file', 'dir\\file', 'file:stream', 'dir./file', 'dir /file']) assert.throws(() => archivePath(root, name));
  assert.equal(archivePath(root, 'emulator/emulator.exe'), path.join(root, 'emulator/emulator.exe'));
});
test('component verification rejects truncated and corrupted files', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-verify-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const file = path.join(dir, 'component'), data = Buffer.from('official content');
  const component = { size: data.length, sha256: createHash('sha256').update(data).digest('hex') };
  assert.equal(await verify(file, component), false);
  await fs.writeFile(file, data); assert.equal(await verify(file, component), true);
  await fs.writeFile(file, Buffer.alloc(data.length)); assert.equal(await verify(file, component), false);
  await fs.writeFile(file, data.subarray(1)); assert.equal(await verify(file, component), false);
});
test('AVD uses hardware graphics and an independently located Android image', () => {
  const config = avdConfig('D:\\Games\\Android', { avd: 'test', cpuCores: 4, memoryMB: 8192 });
  assert.match(config, /hw.gpu.mode=host/); assert.match(config, /hw.cpu.ncore=4/);
  assert.match(config, /disk.dataPartition.size=32G/); assert.match(config, /image.sysdir.1=D:\\Games\\Android/);
});
test('setup refuses missing license or virtualization before touching runtime', async () => {
  const setup = new Setup({ directory: 'unused', runtime: {}, changed() {} });
  await assert.rejects(setup.start({ directory: 'unused', accepted: false }), /license/);
  await assert.rejects(setup.start({ directory: 'unused', accepted: true }), /requirements/);
});
test('failed boot is retryable and never produces a ready receipt', async t => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-setup-')); t.after(() => fs.rm(dir, { recursive: true, force: true }));
  const old = { ...process.env }; t.after(() => { for (const name of ['AXRB_DATA_HOME', 'ANDROID_AVD_HOME', 'ANDROID_USER_HOME', 'ANDROID_HOME', 'ANDROID_SDK_ROOT']) { if (old[name] === undefined) delete process.env[name]; else process.env[name] = old[name]; } });
  let killed = false;
  const avd = path.join(dir, 'AXRB Runtime/avd/axrb-managed-api36.avd');
  await fs.mkdir(avd, { recursive: true }); await fs.writeFile(path.join(avd, 'userdata-qemu.img'), 'fixture');
  const runtime = { settings: { cpuCores: 4, memoryMB: 8192 }, online: async () => false, ensure: async () => { throw new Error('boot failed'); }, adb: async args => { if (args[1] === 'kill') killed = true; return 'unrelated-avd\nOK'; } };
  const setup = new Setup({ root: dir, directory: dir, runtime, components: [], save: async () => {}, changed() {} });
  setup.status.hardware = { hypervisor: true, supportedGpu: true, x64: true, memoryGB: 16 };
  await setup.start({ directory: dir, accepted: true }); await setup.task;
  assert.equal(setup.status.phase, 'error'); assert.match(setup.status.error, /boot failed/); assert.equal(killed, false);
  await assert.rejects(fs.access(path.join(setup.directory, 'ready.json')));
  assert.equal(setup.status.active, false);
});

test('system eligibility accepts AMD without requiring NVIDIA and rejects unsupported systems', () => {
  const amd = { hypervisor: true, supportedGpu: true, x64: true, memoryGB: 16, gpu: 'AMD Radeon' };
  assert.equal(supportedSystem(amd), true);
  assert.equal(supportedSystem({ ...amd, gpu: 'NVIDIA GeForce' }), true);
  for (const patch of [{ supportedGpu: false }, { hypervisor: false }, { x64: false }, { memoryGB: 8 }]) assert.equal(supportedSystem({ ...amd, ...patch }), false);
});

test('setup shows hardware requirements before Hypervisor Platform', () => {
  const weak = { hypervisor: false, supportedGpu: false, x64: true, memoryGB: 8 };
  assert.equal(hardwareRequirementsMet(weak), false);
  assert.equal(setupPhase(weak), 'unsupported');
  assert.equal(setupPhase({ ...weak, supportedGpu: true, memoryGB: 16 }), 'hypervisor');
  assert.equal(setupPhase({ ...weak, hypervisor: true }), 'unsupported');
});

test('debug setup flag skips hardware requirements but keeps the hypervisor gate', async () => {
  const weak = { hypervisor: false, supportedGpu: false, x64: false, memoryGB: 2 };
  assert.equal(setupPhase(weak, { debug: true }), 'hypervisor');
  assert.equal(setupPhase({ ...weak, hypervisor: true }, { debug: true }), 'install');
  const setup = new Setup({ directory: 'unused', runtime: {}, debug: true, changed() {} });
  setup.status.hardware = weak;
  await assert.rejects(setup.start({ directory: 'unused', accepted: true }), /hypervisor/);
});
