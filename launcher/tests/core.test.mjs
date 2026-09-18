import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { MetaAuth, QuestStore, appId, questApp, post } from '../core/meta.mjs';
import { safeName, allowedDownload, downloadFile } from '../core/download.mjs';
import { State } from '../core/state.mjs';
import { powershellArgs, validPackage } from '../core/runtime.mjs';

test('Quest identifiers and platform validation reject Rift and arbitrary hosts', () => {
  assert.equal(appId('https://www.meta.com/experiences/pinball/7255396864545733/'), '7255396864545733');
  assert.throws(() => appId('https://attacker.test/7255396864545733/'));
  assert.throws(() => appId('https://www.meta.com/experiences/rift/7255396864545733/'));
  assert.equal(questApp({ platform: 'PC' }), false);
  assert.equal(questApp({ platform: 'ANDROID' }), false);
  assert.equal(questApp({ platform: 'ANDROID_6DOF' }), true);
});
test('asset paths reject traversal, Windows devices and alternate data streams', () => {
  for (const name of ['../evil', 'a/b', 'a\\b', 'C:evil', 'CON', 'NUL.png', 'bad.', 'bad ']) assert.throws(() => safeName(name));
  assert.equal(safeName('main.12.com.example.game.obb'), 'main.12.com.example.game.obb');
  assert.throws(() => allowedDownload('https://oculus.com.evil.test/file'));
  assert.throws(() => allowedDownload('http://securecdn.oculus.com/file'));
  assert.throws(() => allowedDownload('https://127.0.0.1/file'));
});
test('SSO callbacks are bound to one challenge and do not expose server token errors', async () => {
  const auth = new MetaAuth(); auth.challenge = 'test-secret'; auth.expires = Date.now() + 5000;
  await assert.rejects(auth.complete('oculus://login?token=0000000000000000&blob=test'), /did not match/);
  let calls = 0;
  auth.request = async () => Response.json(++calls === 1 ? { access_token: 'meta-token' } : { data: { xfr_create_profile_token: { profile_tokens: [{ access_token: 'account-token' }] } } });
  const hash = createHash('sha256').update('test-secret').digest('hex').slice(0,16);
  assert.equal(await auth.complete(`oculus://login?token=${hash}&blob=test`), 'account-token');
  await assert.rejects(auth.complete(`oculus://login?token=${hash}&blob=test`), /expired/);
  await assert.rejects(post('https://test', {}, async () => Response.json({ error: { message: 'OC-SECRET-DO-NOT-LOG', code: 190 } })), e => !e.message.includes('SECRET'));
});
test('library keeps only Quest entitlements and identifies incomplete results', async () => {
  const api = new QuestStore('test', async () => Response.json({ data: { viewer: { user: { display_name: 'Player', active_entitlements: { nodes: [
    { item: { id: '123456', display_name: 'Quest game', platform: 'ANDROID_6DOF' } },
    { item: { id: '654321', display_name: 'Rift game', platform: 'PC' } }
  ], page_info: { has_next_page: true } } } } } }));
  const result = await api.library(); assert.equal(result.games.length,1); assert.equal(result.games[0].owned,true); assert.equal(result.partial,true);
});
test('Quest download plan includes base APK, OBB and extra assets once', async () => {
  const api = new QuestStore('test', async (_url, options) => {
    const doc = options.body.get('doc_id');
    if (doc === '2885322071572384') return Response.json({ data: { node: { platform: 'ANDROID_6DOF', primary_binaries: { nodes: [{ id: '123456', version_code: 10 }] } } } });
    if (doc === '4734929166632773') return Response.json({ data: { node: { id: '123456', platform: 'ANDROID_6DOF', package_name: 'com.example.game', version_code: 10, size: '1024', obb_binary: { id: '234567', file_name: 'main.obb', size: '2048' }, asset_files: { nodes: [
      { id:'345678', file_name:'extra.pak', size:'512', is_required:true },
      { id:'345678', file_name:'extra.pak', size:'512', is_required:true },
      { id:'456789', file_name:'optional.pak', size:'512', is_required:false }
    ] } } } });
    assert.fail('Unexpected metadata request');
  });
  const plan = await api.plan('999999'); assert.deepEqual(plan.files.map(f => f.kind), ['apk','obb','asset']);
  assert.equal(plan.package,'com.example.game');
});
test('DLC download availability comes from explicit entitlements', async () => {
  const owned = { id: '222222', display_name: 'Owned', latest_supported_asset_file: { id:'333333', file_name:'extra.pak', size:'20' } };
  const api = new QuestStore('test', async () => Response.json({ data: { node: { active_dlc_entitlements:[{ item: owned }], firstIapItems:{ edges:[{node:owned},{node:{id:'444444',display_name:'Not owned'}}] } } } }));
  const items = await api.dlc('111111'); assert.equal(items.length,2); assert.equal(items[0].owned,true); assert.equal(items[1].owned,false);
});
async function temp(t) { const dir = await fs.mkdtemp(path.join(os.tmpdir(),'axrb-test-')); t.after(() => fs.rm(dir,{recursive:true,force:true})); return dir; }
test('download verifies complete payload and produces a digest', async t => {
  const dir = await temp(t), destination = path.join(dir,'base.apk');
  const result = await downloadFile({ url:'https://securecdn.oculus.com/test', destination, size:4,
    request:async () => new Response('abcd',{headers:{'content-length':'4'}}) });
  assert.equal(await fs.readFile(destination,'utf8'),'abcd'); assert.equal(result.bytes,4);
  assert.equal(result.sha256,createHash('sha256').update('abcd').digest('hex'));
});
test('download trusts streamed bytes when CDN content-length is transformed', async t => {
  const dir = await temp(t), destination = path.join(dir, 'base.apk');
  await downloadFile({ url:'https://securecdn.oculus.com/test', destination, size:4,
    request:async () => new Response('abcd',{headers:{'content-length':'8','content-encoding':'gzip'}}) });
  assert.equal(await fs.readFile(destination, 'utf8'), 'abcd');
});
test('interrupted downloads cannot be marked complete', async t => {
  const dir=await temp(t), destination=path.join(dir,'base.apk');
  await assert.rejects(downloadFile({url:'https://securecdn.oculus.com/test',destination,size:8,request:async()=>new Response('abcd')}),/before the complete/);
  await assert.rejects(fs.access(destination)); assert.equal(await fs.readFile(destination+'.part','utf8'),'abcd');
});
test('resumes only with matching ETag and correct Content-Range', async t => {
  const dir=await temp(t), destination=path.join(dir,'file.obb');
  await fs.writeFile(destination+'.part','ab'); await fs.writeFile(destination+'.part.json',JSON.stringify({size:4,etag:'"v1"'}));
  await downloadFile({url:'https://securecdn.oculus.com/test',destination,size:4,request:async (_url,options)=>{
    assert.equal(options.headers.Range,'bytes=2-'); assert.equal(options.headers['If-Range'],'"v1"');
    return new Response('cd',{status:206,headers:{'content-range':'bytes 2-3/4','content-length':'2',etag:'"v1"'}});
  }}); assert.equal(await fs.readFile(destination,'utf8'),'abcd');
});
test('rejects CDN redirects to local services before issuing the request', async t => {
  const dir=await temp(t);let calls=0;
  await assert.rejects(downloadFile({url:'https://securecdn.oculus.com/test',destination:path.join(dir,'file'),request:async()=>{calls++;return new Response(null,{status:302,headers:{location:'http://localhost:1234'}});}}),/Meta delivery/);
  assert.equal(calls,1);
});
test('library writes serialize and interrupted jobs are recoverable', async t => {
  const dir=await temp(t), state=new State(dir);await state.load();state.put({id:'1',name:'Game'});
  const first=state.save();state.data.jobs.push({id:'job',status:'downloading'});await state.save();await first;
  const reopened=new State(dir);await reopened.load();assert.equal(reopened.data.games[0].name,'Game');assert.equal(reopened.data.jobs[0].status,'interrupted');
});
test('PowerShell paths and game names are literal, including quotes and substitutions', () => {
  const args=powershellArgs('C:\\project\\run.ps1',{GameName:"A 'quote' $(Get-Content secret)",GpuSharing:true});
  const script=Buffer.from(args.at(-1),'base64').toString('utf16le');assert.ok(script.includes("'A ''quote'' $(Get-Content secret)'"));assert.ok(script.includes('-GpuSharing:$true'));
  assert.throws(()=>validPackage('com.game;rm'));assert.equal(validPackage('com.example.game'),'com.example.game');
});
