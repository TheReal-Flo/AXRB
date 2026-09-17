import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Runtime } from '../core/runtime.mjs';

test('Windows game launch actually executes PowerShell and reports its exit', { skip: process.platform !== 'win32', timeout: 15000 }, async t => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'axrb-launch-'));
  t.after(() => fs.rm(root, { recursive: true, force: true }));
  await fs.mkdir(path.join(root, 'tools'));
  await fs.writeFile(path.join(root, 'tools/run_windows_game.ps1'), `
param($Avd, $Port, $Sdk, $MemoryMB, $Package, $Activity, $GameName)
Write-Output "EXECUTED:$Package"
exit 7
`);
  const runtime = new Runtime(root, { avd:'test',port:5580,sdk:root,memoryMB:8192 });
  const result = await new Promise(resolve => {
    runtime.launch({ id:'local:com.example.game',package:'com.example.game',activity:'com.example.game/.Main',name:'Test' }, (code, output) => resolve({code,output}));
    t.after(() => runtime.child?.kill());
  });
  assert.equal(result.code, 1); // PowerShell wrapper normalizes failed script exits.
  assert.match(result.output, /EXECUTED:com.example.game/);
  assert.equal(runtime.game, null);
});
