import assert from 'node:assert/strict';

// Only used by --smoke-test in its isolated profile. Native dialogs and Android
// mutations are replaced here; production IPC and React run unchanged.
export async function libraryActionsSmoke(window, { state, runtime, dialog, persist, publicState }) {
  const wc = window.webContents, js = code => wc.executeJavaScript(code);
  const wait = () => new Promise(r => setTimeout(r, 50));
  async function check(code, message) {
    for (let n = 0; n < 100; n++) { if (await js(code)) return; await wait(); }
    throw Error(message);
  }
  const original = { open: dialog.showOpenDialog, confirm: dialog.showMessageBox, inspect: runtime.inspect, uninstall: runtime.uninstall };
  const game = { id: 'local:com.axrbtest.game', package: 'com.axrbtest.game', name: 'Uninstall test', installed: true, source: 'local', apk: 'C:/test/base.apk', files: [{ path: 'C:/test/content.obb' }] };
  let removed = 0, confirmation;
  try {
    state.put(game); await persist();
    await js(`document.querySelector('[data-nav="library"]').click()`);
    // Remove the previous smoke filter by opening the dialog through a fresh state.
    await js(`document.querySelector('[aria-label="Filter library"]').click()`);
    await check(`Boolean(document.querySelector('[role="option"]'))`, 'Filter did not open');
    await js(`Array.from(document.querySelectorAll('[role="option"]')).find(e => e.textContent === 'All games').click()`);
    await check(`Boolean(document.querySelector('[data-game="${game.id}"]'))`, 'Installed game missing');
    await js(`document.querySelector('[data-game="${game.id}"]').click()`);
    await check(`Boolean(document.querySelector('[aria-label="Game actions"]'))`, 'Actions missing');
    // Dispatch pointerdown as required by the Radix dropdown trigger.
    await js(`document.querySelector('[aria-label="Game actions"]').dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, pointerType: 'mouse', ctrlKey: false }))`);
    await check(`Array.from(document.querySelectorAll('[role="menuitem"]')).some(e => e.textContent === 'Uninstall')`, 'Uninstall action missing');
    dialog.showMessageBox = async (_window, options) => { confirmation = options; return { response: 0 }; };
    runtime.uninstall = async () => { removed++; };
    await js(`Array.from(document.querySelectorAll('[role="menuitem"]')).find(e => e.textContent === 'Uninstall').click()`);
    await check(`!document.querySelector('[role="menu"]')`, 'Uninstall menu did not close');
    await wait(); assert.equal(removed, 0); assert.equal(game.installed, true);
    assert.match(confirmation.detail, /saved data/); assert.equal(confirmation.defaultId, 0);
    dialog.showMessageBox = async () => ({ response: 1 });
    const result = await js(`window.axrb.uninstall('${game.id}')`);
    assert.equal(result.ok, true); assert.equal(result.value, true); assert.equal(removed, 1);
    assert.equal(game.installed, false); assert.equal(game.apk, 'C:/test/base.apk'); assert.equal(game.files.length, 1);
    await check(`Array.from(document.querySelectorAll('#details button')).some(e => e.textContent === 'Install')`, 'Uninstall did not update library');
    await check(`document.querySelector('[data-job-toast]')?.textContent.includes('uninstalled')`, 'Uninstall completion toast missing');
    game.installed = true; runtime.uninstall = async () => { throw Error('Android uninstall rejected'); }; await persist();
    await js(`window.axrb.uninstall('${game.id}')`); assert.equal(game.installed, true);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Android uninstall rejected'))`, 'Uninstall failure toast missing');
    dialog.showOpenDialog = async () => ({ canceled: false, filePaths: ['C:/test/import.apk'] });
    runtime.inspect = async () => { await new Promise(r => setTimeout(r, 250)); return { id: 'local:com.axrbtest.imported', package: 'com.axrbtest.imported', name: 'APK toast test', source: 'local', apk: 'C:/test/import.apk' }; };
    await js(`window.axrb.import()`);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Reading APK'))`, 'APK progress toast missing');
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('APK toast test: imported'))`, 'APK completion toast missing');
    runtime.inspect = async () => { throw Error('Invalid APK fixture'); };
    await js(`window.axrb.import()`);
    await check(`Array.from(document.querySelectorAll('[data-job-toast]')).some(e => e.textContent.includes('Invalid APK fixture'))`, 'APK failure toast missing');
    const snapshot = publicState(), zip = { id: 'zip-toast', kind: 'zip', name: 'ZIP test', status: 'downloading', stage: 'Extracting ZIP', total: 10, completed: 4, progressUnit: 'files' };
    snapshot.jobs = [zip, ...snapshot.jobs]; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"] progress')?.value === 4`, 'ZIP progress toast missing');
    zip.status = 'installing'; zip.stage = 'Installing APK'; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"]')?.textContent.includes('Installing APK')`, 'ZIP install stage missing');
    await js(`document.querySelector('[data-job-toast="zip-toast"] button').click()`);
    wc.send('axrb:changed', snapshot); await wait();
    assert.equal(await js(`Boolean(document.querySelector('[data-job-toast="zip-toast"]'))`), false);
    zip.status = 'complete'; wc.send('axrb:changed', snapshot);
    await check(`document.querySelector('[data-job-toast="zip-toast"]')?.textContent.includes('ZIP test: installed')`, 'ZIP completion missing after dismissed progress');
    console.log('Library actions smoke passed: uninstall cancel/success/failure, retained downloads, APK and ZIP toast lifecycle.');
  } finally {
    dialog.showOpenDialog = original.open; dialog.showMessageBox = original.confirm; runtime.inspect = original.inspect; runtime.uninstall = original.uninstall;
    state.data.games = state.data.games.filter(g => !g.package?.startsWith('com.axrbtest.'));
    state.data.jobs = state.data.jobs.filter(j => !['apk', 'uninstall'].includes(j.kind)); await persist();
  }
}
