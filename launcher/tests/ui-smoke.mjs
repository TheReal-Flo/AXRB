import fs from 'node:fs/promises';
import path from 'node:path';

// Exercise the production bundle in sandboxed Electron. Uses a separate profile;
// no authentication, installs, patching, or game launches.
export async function uiSmoke(window, directory, snapshot, errors) {
  const wc = window.webContents;
  const js = code => wc.executeJavaScript(code);
  const tick = () => new Promise(resolve => setTimeout(resolve, 60));
  async function check(code, message) {
    for (let i = 0; i < 100; i++) { if (await js(code)) return; await tick(); }
    throw new Error(message);
  }
  async function click(selector) { await js(`document.querySelector(${JSON.stringify(selector)}).focus(); document.querySelector(${JSON.stringify(selector)}).click()`); await tick(); }
  async function input(selector, text) {
    await js(`{
      const el = document.querySelector(${JSON.stringify(selector)});
      el.focus();
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(text)});
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }`);
    await tick();
  }
  async function pointer(selector) {
    const bounds = await js(`(() => { const r = document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect(); return { x: Math.round(r.x+r.width/2), y: Math.round(r.y+r.height/2) }; })()`);
    wc.sendInputEvent({ type: 'mouseDown', button: 'left', clickCount: 1, ...bounds });
    wc.sendInputEvent({ type: 'mouseUp', button: 'left', clickCount: 1, ...bounds });
    await tick();
  }
  async function escape() { wc.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' }); wc.sendInputEvent({ type: 'keyUp', keyCode: 'Escape' }); await tick(); }
  async function capture(name) { await tick(); await fs.writeFile(path.join(directory, `${name}.png`), (await wc.capturePage()).toPNG()); }

  await check(`Boolean(document.querySelector('#library-search'))`, 'Library did not load');
  await check(`Array.from(document.querySelectorAll('button')).some(b => b.textContent === 'Install ZIP')`, 'ZIP install action missing');
  if (await js(`Boolean(document.querySelector('h1, footer'))`)) throw new Error('Unexpected decorative heading/footer');
  await click('[data-nav="quest"]');
  await check(`Boolean(document.querySelector('[data-quest]'))`, 'Quest view did not render');
  await check(`Boolean(document.querySelector('[aria-label="Quest device"]')) && Boolean(document.querySelector('[aria-label="Refresh Quest"]'))`, 'Quest controls missing');
  await capture('quest');
  await click('[data-nav="settings"]');
  await check(`Boolean(document.querySelector('#settings-form'))`, 'Settings did not render');
  await input('#downloadDir', 'C:\\AXRB UI draft');
  wc.send('axrb:changed', snapshot());
  await tick();
  await check(`document.querySelector('#downloadDir').value === 'C:\\\\AXRB UI draft'`, 'State updates erased settings draft');
  await click('[data-runtime-settings] summary');
  await check(`document.querySelector('[data-runtime-settings]').open`, 'Runtime settings did not expand');
  const originalHud = await js(`document.querySelector('#fps-hud').checked`);
  await click('#fps-hud');
  await check(`document.querySelector('#fps-hud').checked !== ${originalHud}`, 'FPS HUD toggle did not update');
  await click('#fps-hud');
  await check(`document.querySelector('#fps-hud').checked === ${originalHud}`, 'FPS HUD toggle did not restore');
  await capture('settings');
  await click('[data-nav="downloads"]');
  await check(`Boolean(document.querySelector('.jobs'))`, 'Downloads did not render');
  await click('[data-nav="store"]');
  await input('#store-query', 'Pinball');
  await js(`document.querySelector('#store-search').requestSubmit()`);
  for (let i = 0; i < 150; i++) { if (await js(`Boolean(document.querySelector('[data-store="true"]'))`)) break; await new Promise(r => setTimeout(r, 200)); }
  const count = await js(`document.querySelectorAll('[data-store="true"]').length`);
  if (!count) throw new Error('Live Quest search returned no games');
  await new Promise(r => setTimeout(r, 1200));
  await capture('store');
  await click('[data-store="true"]');
  await check(`Boolean(document.querySelector('#details[role="dialog"]'))`, 'Game dialog did not open');
  await capture('details');
  // Add only to the isolated smoke profile. This never purchases/downloads a game.
  await js(`Array.from(document.querySelectorAll('#details button')).find(b => b.textContent.includes('Add to library'))?.click()`);
  await check(`Boolean(document.querySelector('[aria-label="Game actions"]'))`, 'Library add did not update open dialog');
  await pointer('[aria-label="Game actions"]');
  await check(`Boolean(document.querySelector('[role="menu"]'))`, 'Game actions menu did not open');
  await escape();
  await escape();
  await check(`!document.querySelector('#details')`, 'Escape did not close dialog');
  await check(`document.activeElement.matches('[data-store="true"]')`, 'Dialog did not restore keyboard focus');
  await click('[data-nav="library"]');
  await check(`Boolean(document.querySelector('[data-game]'))`, 'Added game missing from library');
  await capture('window');
  await input('#library-search', 'no-match-axrb-test');
  wc.send('axrb:changed', snapshot());
  await tick();
  await check(`document.activeElement.id === 'library-search' && document.querySelector('#library-search').value === 'no-match-axrb-test' && !document.querySelector('[data-game]')`, 'Library filter/focus lost during state update');
  await input('#library-search', '');
  await pointer('[aria-label="Filter library"]');
  await check(`Boolean(document.querySelector('[role="listbox"]'))`, 'Filter did not open');
  wc.sendInputEvent({ type: 'keyDown', keyCode: 'Down' });
  wc.sendInputEvent({ type: 'keyUp', keyCode: 'Down' });
  await tick();
  await check(`document.activeElement.textContent === 'Installed'`, 'Keyboard did not focus Installed filter');
  wc.sendInputEvent({ type: 'keyDown', keyCode: 'Enter' });
  wc.sendInputEvent({ type: 'keyUp', keyCode: 'Enter' });
  await tick();
  await check(`!document.querySelector('[data-game]')`, 'Installed filter did not hide uninstalled game');

  const fixture = snapshot();
  fixture.jobs = [{ id: 'ui-download', gameId: '123456', name: 'Download test', status: 'downloading', stage: 'base.apk', completed: 100, total: 200 }];
  wc.send('axrb:changed', fixture);
  await click('[data-nav="downloads"]');
  await check(`document.querySelector('progress')?.value === 100`, 'Download progress did not render');
  await capture('downloads');
  window.setSize(920, 640);
  await tick();
  await click('[data-nav="library"]');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'Library import controls overflow minimum window width');
  await capture('library-small');
  await click('[data-nav="store"]');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'UI overflows minimum window width');
  await capture('store-small');
  wc.send('axrb:launch-error', 'Game launch test failed');
  await check(`document.querySelector('[role="alert"]')?.textContent.includes('Game launch test failed')`, 'Launch failure was not shown');
  const setup = { phase: 'hypervisor', directory: 'C:\\AXRB Runtime', active: false };
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.body.textContent.includes('Enable Windows Hypervisor Platform')`, 'Virtualization instructions missing');
  await capture('setup-hypervisor');
  setup.phase = 'install';
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`Boolean(document.querySelector('#runtime-folder'))`, 'Setup folder missing');
  await check(`Array.from(document.querySelectorAll('button')).find(b => b.textContent === 'Download and set up')?.disabled`, 'Setup did not require license acceptance');
  await click('input[type="checkbox"]');
  await check(`!Array.from(document.querySelectorAll('button')).find(b => b.textContent === 'Download and set up')?.disabled`, 'License acceptance did not enable setup');
  await capture('setup-install');
  Object.assign(setup, { phase: 'download', active: true, component: 'Android 16 with ARM64 translation', completed: 512, total: 1024 });
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('progress')?.value === 512 && document.body.textContent.includes('50%')`, 'Setup progress missing');
  await capture('setup-download');
  Object.assign(setup, { phase: 'error', active: false, error: 'Download interrupted' });
  wc.send('axrb:changed', { ...snapshot(), setup });
  await check(`document.querySelector('[role="alert"]')?.textContent.includes('Download interrupted') && document.body.textContent.includes('Retry setup')`, 'Setup retry missing');
  await check(`document.documentElement.scrollWidth <= window.innerWidth`, 'Setup overflows minimum window width');
  if (errors.length) throw new Error(`Renderer errors: ${errors.join('; ')}`);
  console.log(`AXRB React smoke passed: navigation, settings draft, ${count} live Quest results, library add/filter, dialog/menu keyboard focus, download progress, 920px layout`);
}
