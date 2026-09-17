import test from 'node:test';
import assert from 'node:assert/strict';
import { QuestStore } from '../core/meta.mjs';
import { State } from '../core/state.mjs';
import { loadLibraryArtwork } from '../core/artwork.mjs';

test('owned artwork uses the exact public listing and hero image without credentials', async () => {
  const api = new QuestStore('private-token', async (_url, options) => {
    assert.equal(options.body.has('access_token'), false);
    assert.equal(JSON.parse(options.body.get('variables')).itemId, '123456');
    return Response.json({ data: { item: { id:'123456', hero:{uri:'https://cdn.oculuscdn.com/art.jpg'} } } });
  });
  assert.equal(await api.artwork('123456'), 'https://cdn.oculuscdn.com/art.jpg');
  api.request = async () => Response.json({ data:{ item:{id:'654321',hero:{uri:'wrong'}} } });
  await assert.rejects(api.artwork('123456'), /requested artwork/);
});

test('library refresh preserves artwork, ownership and installed state through enrichment', async () => {
  const state = new State('unused');
  state.put({id:'123456',source:'meta',owned:true,installed:true,image:'cached'});
  state.put({id:'123456',source:'meta',owned:true,image:''});
  assert.equal(state.data.games[0].image,'cached');
  await loadLibraryArtwork(state.data.games, {artwork:async()=> 'new-art'}, async(id,patch)=>state.put({id,...patch}));
  assert.equal(state.data.games[0].image,'new-art');
  assert.equal(state.data.games[0].owned,true);
  assert.equal(state.data.games[0].installed,true);
});

test('artwork requests are bounded, cached, and do not erase images on failure', async () => {
  const now = Date.now();
  const games = Array.from({length:8},(_,i)=>({id:String(100000+i),source:'meta',image:i===0?'cached':''}));
  games.push({id:'local:game',source:'installed'}, {id:'999999',source:'meta',image:'fresh',artworkCheckedAt:now});
  let active=0, peak=0, calls=0;
  const api = {artwork:async()=>{ calls++; active++; peak=Math.max(peak,active); await new Promise(r=>setTimeout(r,2)); active--; throw new Error('offline'); }};
  await loadLibraryArtwork(games,api,async(id,patch)=>Object.assign(games.find(g=>g.id===id),patch),now);
  assert.equal(calls,8); assert.ok(peak<=3); assert.equal(games[0].image,'cached');
  await loadLibraryArtwork(games,api,async()=>assert.fail('retry too soon'),now+1000);
  assert.equal(calls,8);
});
