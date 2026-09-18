import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { assetDestination, classifyAsset, importGameZip, inspectGameFolder, installFiles, shellQuote } from '../core/game-files.mjs';
import { parseDevices, Quest } from '../core/quest.mjs';
import { run } from '../core/runtime.mjs';
import { State } from '../core/state.mjs';

const pkg = 'com.example.game';
const inspect = async file => ({ id: `local:${pkg}`, source: 'local', package: pkg, name: 'Example Game', activity: `${pkg}/.Main`,
  versionCode: '7', version: '1.0', split: path.basename(file).startsWith('split') ? 'arm64' : '', apk: file });
async function workspace(t) {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-import-'));
  t.after(() => fs.rm(directory, { recursive: true, force: true })); return directory;
}
async function zip(file, entries) {
  await fs.writeFile(`${file}.json`, JSON.stringify(entries));
  await promisify(execFile)('python', ['-c', 'import sys,json,zipfile\nwith zipfile.ZipFile(sys.argv[1],"w",compression=zipfile.ZIP_DEFLATED) as z:\n for name,body in json.load(open(sys.argv[2],encoding="utf-8")): z.writestr(name,body)', file, `${file}.json`], { windowsHide: true });
}

test('assets retain package-specific destinations and reject escape/ambiguous layouts', () => {
  assert.equal(classifyAsset(`Game/Android/data/${pkg}/files/a.bundle`, pkg), `Android/data/${pkg}/files/a.bundle`);
  assert.equal(classifyAsset(`obb/${pkg}/main.7.${pkg}.obb`, pkg), `Android/obb/${pkg}/main.7.${pkg}.obb`);
  assert.equal(classifyAsset(`Game/main.7.${pkg}.obb`, pkg), `Android/obb/${pkg}/main.7.${pkg}.obb`);
  for (const name of [`Android/data/com.other.game/files/a`, `Android/data/${pkg}/../other/a`, `Android/data/${pkg}/files/CON`, `Android/data/${pkg}/files/a:stream`, `Android/data/${pkg}//a`, `Android/data/${pkg}/./a`]) assert.throws(() => assetDestination(pkg, name));
  assert.throws(() => classifyAsset('unknown.bundle', pkg), /Cannot determine/);
  assert.throws(() => classifyAsset('main.7.com.other.game.obb', pkg), /does not match/);
  assert.equal(shellQuote("a'b"), "'a'\\''b'");
});

test('ZIP import keeps splits, nested assets, identity and integrity hashes', async t => {
  const dir = await workspace(t), archive = path.join(dir, 'game.zip');
  await zip(archive, [['Game/base.output.apk', 'base'], ['Game/split_config.arm64.apk', 'split'],
    [`Game/Android/data/${pkg}/files/a.bundle`, 'data'], [`Game/main.7.${pkg}.obb`, 'expansion']]);
  const stages = [], progress = [];
  const game = await importGameZip(archive, dir, inspect, { reserve: 0, update: s => stages.push(s), progress: (...v) => progress.push(v) });
  assert.equal(game.package, pkg); assert.equal(game.files.length, 4); assert.equal(game.downloaded, true);
  assert.equal(game.files.filter(f => f.kind === 'split').length, 1);
  assert.ok(game.files.every(f => /^[a-f0-9]{64}$/.test(f.sha256)));
  assert.equal(game.files.find(f => f.name.endsWith('a.bundle')).destination, `Android/data/${pkg}/files/a.bundle`);
  assert.equal(await fs.readFile(game.apk, 'utf8'), 'base');
  assert.ok(stages.includes('Extracting ZIP')); assert.deepEqual(progress.at(-1), [4, 4]);
  const library = new State(path.join(dir, 'state')); await library.load();
  library.put({ id: '12345', package: pkg, name: 'Store name', source: 'meta', installed: true });
  const existing = library.data.games[0];
  library.put({ ...game, id: existing.id, source: existing.source }); await library.save();
  const reloaded = new State(path.join(dir, 'state')); await reloaded.load();
  assert.equal(reloaded.data.games.length, 1); assert.equal(reloaded.data.games[0].id, '12345');
  assert.equal(reloaded.data.games[0].files.length, 4); assert.equal(reloaded.data.games[0].installed, true);
});

test('invalid and cancelled ZIP imports remove their partial directory', async t => {
  const dir = await workspace(t);
  for (const [label, entries] of [
    ['traversal', [['../escape', 'bad']]], ['duplicate', [['base.apk', 'a'], ['BASE.APK', 'b']]],
    ['two-games', [['base.apk', 'a'], ['other.apk', 'b']]],
    ['wrong-assets', [['base.apk', 'a'], ['Android/obb/com.other.game/data', 'bad']]],
    ['ambiguous', [['base.apk', 'a'], ['data.bundle', 'unknown']]]
  ]) {
    const archive = path.join(dir, `${label}.zip`); await zip(archive, entries);
    await assert.rejects(importGameZip(archive, dir, inspect, { reserve: 0 }));
    assert.deepEqual(await fs.readdir(path.join(dir, 'imports')).catch(() => []), []);
  }
  const archive = path.join(dir, 'cancel.zip'); await zip(archive, [['base.apk', 'a']]);
  const controller = new AbortController(); controller.abort();
  await assert.rejects(importGameZip(archive, dir, inspect, { reserve: 0, signal: controller.signal }));
  assert.deepEqual(await fs.readdir(path.join(dir, 'imports')), []);
});

test('split packages and versions must match before installation', async t => {
  const dir = await workspace(t); await fs.writeFile(path.join(dir, 'base.apk'), 'base'); await fs.writeFile(path.join(dir, 'split.apk'), 'split');
  for (const field of ['package', 'versionCode']) await assert.rejects(inspectGameFolder(dir, async file => ({ ...await inspect(file), ...(path.basename(file) === 'split.apk' ? { [field]: 'different' } : {}) })), /same game and version/);
});

test('ZIP checksum failure and cancellation during extraction clean up safely', async t => {
  const dir = await workspace(t), archive = path.join(dir, 'game.zip');
  await zip(archive, [['base.apk', 'apk'], [`Android/data/${pkg}/files/blob`, 'x'.repeat(2000000)]]);
  const controller = new AbortController();
  await assert.rejects(importGameZip(archive, dir, inspect, { signal: controller.signal, progress: () => controller.abort() }));
  assert.deepEqual(await fs.readdir(path.join(dir, 'imports')), []);
  const bytes = await fs.readFile(archive), central = bytes.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02]));
  bytes[central + 16] ^= 1; await fs.writeFile(archive, bytes);
  await assert.rejects(importGameZip(archive, dir, inspect, { reserve: 0 }), /checksum/);
  assert.deepEqual(await fs.readdir(path.join(dir, 'imports')), []);
});

test('installation uses an APK session for splits and preserves asset destinations', async t => {
  const dir = await workspace(t), apk = path.join(dir, 'base.apk'), split = path.join(dir, 'split.apk'), asset = path.join(dir, 'asset');
  for (const file of [apk, split, asset]) await fs.writeFile(file, 'test');
  const game = { package: pkg, apk, files: [{ name: 'split.apk', path: split, kind: 'split' }, { name: "a'b.bundle", path: asset, kind: 'asset', destination: `Android/data/${pkg}/files/a'b.bundle` }] };
  const calls = [], adb = async args => { calls.push(args); return args.includes('df') ? '/dev/data 90000000 1000 80000000 1% /data' : 'Success'; };
  await installFiles(game, adb);
  assert.deepEqual(calls[1], ['install-multiple', '--no-incremental', '--force-queryable', '-r', apk, split]);
  assert.ok(calls.some(c => c[0] === 'push' && c[2] === `/sdcard/Android/data/${pkg}/files/a'b.bundle`));
  assert.deepEqual(calls.at(-1), ['shell', 'sync']);
  calls.length = 0;
  await assert.rejects(installFiles(game, async args => { calls.push(args); return '/dev/data 1000 900 100 90% /data'; }), /Android needs/);
  assert.equal(calls.length, 1);
  game.files[1].destination = 'Android/data/com.other.app/files/x'; calls.length = 0;
  await assert.rejects(installFiles(game, adb), /does not belong/); assert.equal(calls.length, 0);
});

test('existing store assets still install to their original OBB filenames', async t => {
  const dir = await workspace(t), file = path.join(dir, 'file'); await fs.writeFile(file, 'data');
  const calls = [];
  await installFiles({ package: pkg, apk: file, files: [{ kind: 'asset', name: '123.asset', path: file }] }, async args => {
    calls.push(args); return args.includes('df') ? '/dev/data 90000000 1000 80000000 1% /data' : 'ok';
  });
  assert.equal(calls[1][0], 'install');
  assert.ok(calls.some(c => c[0] === 'push' && c[2] === `/sdcard/Android/obb/${pkg}/123.asset`));
});

function deviceFixture({ disconnect = false, denyAssets = false, truncate = false } = {}) {
  const calls = [], files = new Map([
    ['/data/app/game/base.apk', 'base'], ['/data/app/game/split_config.arm64.apk', 'split'],
    [`/sdcard/Android/obb/${pkg}/main.7.${pkg}.obb`, 'expansion'], [`/sdcard/Android/data/${pkg}/files/a.bundle`, 'data']
  ]);
  const execute = async (_exe, args, options = {}) => {
    calls.push(args);
    if (args[0] === 'devices') return 'List of devices attached\nQUEST123 device product:hollywood model:Quest_2\nemulator-5582 device\n';
    assert.deepEqual(args.slice(0, 2), ['-s', 'QUEST123']);
    const op = args.slice(2), command = op.slice(1).join(' ');
    if (op[0] === 'pull') {
      if (disconnect) throw new Error('device disconnected');
      await fs.writeFile(op[2], truncate ? '' : files.get(op[1])); options.onOutput?.('[100%] file'); return '';
    }
    if (op[0] === 'push') return '';
    if (command.includes('ro.product.model')) return 'Quest 2';
    if (command.includes('ro.product.manufacturer')) return 'Oculus';
    if (command === 'pm list packages -3') return `package:${pkg}\n`;
    if (command.startsWith('CLASSPATH=')) return JSON.stringify([{ package: pkg, name: 'Example Game' }]);
    if (command.startsWith('rm -f')) return '';
    if (command.startsWith('pm path')) return 'package:/data/app/game/base.apk\npackage:/data/app/game/split_config.arm64.apk\n';
    if (command.startsWith('stat')) return String(files.get(command.match(/'([^']+)'/)[1]).length);
    if (command.startsWith('ls')) { if (denyAssets) throw new Error('Permission denied'); return `${pkg}\n`; }
    if (command.startsWith('find')) return [...files].filter(([remote]) => remote.startsWith(command.match(/'([^']+)'/)[1])).map(([remote, data]) => `${data.length} ${remote}`).join('\n');
    throw new Error(`Unexpected command ${command}`);
  };
  return { quest: new Quest({ sdk: 'sdk' }, execute), calls };
}

test('Quest enumeration excludes emulator and imports named apps with all splits/assets', async t => {
  const dir = await workspace(t), { quest, calls } = deviceFixture();
  assert.deepEqual(await quest.games('QUEST123'), [{ package: pkg, name: 'Example Game' }]);
  const progress = [];
  const game = await quest.pullGame('QUEST123', pkg, dir, inspect, { reserve: 0, progress: (...v) => progress.push(v) });
  assert.equal(game.source, 'quest'); assert.equal(game.files.length, 4); assert.equal(game.name, 'Example Game');
  assert.equal(calls.filter(c => c[2] === 'pull').length, 4);
  assert.deepEqual(progress.at(-1), [22, 22]);
  assert.equal(parseDevices('emulator-5582 device\nQ unauthorized\nR offline\n').length, 2);
  await assert.rejects(quest.requireDevice('emulator-5582'), /Connect/);
  assert.throws(() => quest.device('Q;bad', ['shell']), /Invalid/);
});

test('disconnected, truncated and inaccessible Quest transfers never publish partial games', async t => {
  const dir = await workspace(t);
  for (const options of [{ disconnect: true }, { truncate: true }, { denyAssets: true }]) {
    const { quest } = deviceFixture(options);
    await assert.rejects(quest.pullGame('QUEST123', pkg, dir, inspect, { reserve: 0 }));
    assert.deepEqual(await fs.readdir(path.join(dir, 'quest', pkg)).catch(() => []), []);
  }
});

test('cancellation terminates the transfer subprocess before returning', async () => {
  const controller = new AbortController();
  const task = run(process.execPath, ['-e', 'setInterval(() => {}, 1000)'], { signal: controller.signal });
  controller.abort(); await assert.rejects(task, /Cancelled/);
});
