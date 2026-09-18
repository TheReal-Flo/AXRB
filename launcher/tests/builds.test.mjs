import test from 'node:test';
import assert from 'node:assert/strict';
import {QuestStore} from '../core/meta.mjs';

test('AndroidBinary versions without platform metadata use Quest listing and published release',async()=>{
 const api=new QuestStore('test',async(_url,{body})=>{
  if(body.get('doc_id')==='2885322071572384')return Response.json({data:{node:{primary_binaries:{nodes:[
   {id:'111111',__typename:'AndroidBinary',version_code:99},
   {id:'222222',__typename:'AndroidBinary',version_code:12},
   {id:'333333',__typename:'RiftBinary',platform:'PC',version_code:100}
  ]}}}});
  assert.equal(JSON.parse(body.get('variables')).hmdType,'EUREKA');
  return Response.json({data:{item:{id:'123456',supported_hmd_platforms:['EUREKA'],latest_supported_binary:{id:'222222',__typename:'AndroidBinary',version:'Released'}}}});
 });
 const builds=await api.builds('123456');
 assert.deepEqual(builds.map(b=>b.id),['222222','111111']);
 assert.equal(builds[0].version,'Released');
});

test('Android binary type alone does not establish Quest platform',async()=>{
 const api=new QuestStore('test',async(_url,{body})=>Response.json(body.get('doc_id')==='2885322071572384'?{data:{node:{primary_binaries:{nodes:[{id:'111111',__typename:'AndroidBinary'}]}}}}:{data:{item:{id:'123456',platform:'PC'}}}));
 await assert.rejects(api.builds('123456'),/Quest platform/);
});

test('delivery authentication is restricted to securecdn binary endpoints',()=>{
 const api=new QuestStore('test-secret');
 const u=new URL(api.fileUrl({uri:'https://securecdn-dus1-1.oculus.com/binaries/download/?id=123456'}));
 assert.equal(u.searchParams.get('access_token'),'test-secret');
 for(const uri of ['https://securecdn.oculus.com.evil.test/binaries/download/?id=123456','https://other.oculus.com/binaries/download/?id=123456','https://cdn.fbcdn.net/signed?sig=abc'])assert.ok(!api.fileUrl({uri}).includes('test-secret'));
});

test('Quest delivery exchanges existing profile sessions once and keeps metadata credentials',async()=>{
 let calls=0;
 const api=new QuestStore('profile-secret',async(url,options)=>{
  calls++;const parsed=new URL(url);
  assert.equal(parsed.origin+parsed.pathname,'https://graph.oculus.com/authenticate_application');
  assert.equal(parsed.searchParams.get('app_id'),'1481000308606657');
  assert.equal(parsed.searchParams.get('access_token'),'profile-secret');
  assert.equal(options.method,'POST');
  return Response.json({access_token:'quest-secret'});
 });
 const urls=await Promise.all([api.downloadUrl({id:'123456'}),api.downloadUrl({uri:'https://securecdn-dus1-1.oculus.com/binaries/download/?id=234567'})]);
 assert.equal(calls,1);assert.equal(api.token,'profile-secret');
 for(const url of urls)assert.equal(new URL(url).searchParams.get('access_token'),'quest-secret');
});

test('failed Quest exchange can retry and does not expose server credentials',async()=>{
 let calls=0;const api=new QuestStore('private',async()=>++calls===1?Response.json({error:{code:190,message:'private'}}):Response.json({access_token:'quest'}));
 await assert.rejects(api.downloadUrl({id:'123456'}),error=>/expired/.test(error.message)&&!error.message.includes('private'));
 assert.equal(new URL(await api.downloadUrl({id:'123456'})).searchParams.get('access_token'),'quest');
});
