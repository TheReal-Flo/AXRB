import test from 'node:test';
import assert from 'node:assert/strict';
import { Runtime } from '../core/runtime.mjs';

function fixture() {
  const runtime = new Runtime('test', { port: 5582, avd: 'axrb' }), calls = [];
  runtime.ensure = async () => calls.push('ensure');
  runtime.installed = async () => new Set(['com.example.game']);
  runtime.adb = async args => { calls.push(args); return 'Success\r\n'; };
  return { runtime, calls };
}
test('uninstall targets the game package without retaining app data or touching local files', async () => {
  const { runtime, calls } = fixture();
  const game = { package: 'com.example.game', apk: 'C:/downloads/base.apk', files: [{ path: 'C:/downloads/content.obb' }] };
  const original = structuredClone(game), stages = [];
  await runtime.uninstall(game, stage => stages.push(stage));
  assert.deepEqual(calls, ['ensure', ['uninstall', 'com.example.game']]);
  assert.deepEqual(game, original); assert.deepEqual(stages, ['Starting Android', 'Uninstalling']);
});
test('uninstall refuses running games, invalid packages and the runtime itself', async () => {
  const { runtime, calls } = fixture();
  for (const name of ['com.axrb.openxrruntime', 'com.example.game;rm', '../game']) await assert.rejects(runtime.uninstall({ package: name }));
  runtime.child = {};
  await assert.rejects(runtime.uninstall({ package: 'com.example.game' }), /Close the running game/);
  assert.deepEqual(calls, []);
});
test('uninstall fails safely for absent packages, disconnection and Android rejection', async () => {
  const { runtime, calls } = fixture();
  runtime.installed = async () => new Set();
  await assert.rejects(runtime.uninstall({ package: 'com.example.game' }), /not installed/);
  runtime.installed = async () => null;
  await assert.rejects(runtime.uninstall({ package: 'com.example.game' }), /disconnected/);
  assert.ok(calls.every(c => c === 'ensure'));
  runtime.installed = async () => new Set(['com.example.game']);
  runtime.adb = async () => 'Failure [DELETE_FAILED_INTERNAL_ERROR]';
  await assert.rejects(runtime.uninstall({ package: 'com.example.game' }), /DELETE_FAILED/);
});
