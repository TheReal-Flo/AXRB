const { spawn } = require('node:child_process');
const environment = { ...process.env };
// Editors may set this for their own helpers; AXRB needs Electron's GUI mode.
delete environment.ELECTRON_RUN_AS_NODE;
// Build on launch so both npm start and the desktop shortcut use current sources.
const build = spawn(process.execPath, [require('node:path').join(require.resolve('vite/package.json'), '..', 'bin', 'vite.js'), 'build'], {
  cwd: __dirname, env: environment, stdio: 'inherit', windowsHide: true
});
build.on('error', error => { console.error(error.message); process.exit(1); });
build.on('exit', code => {
  if (code !== 0) return process.exit(code ?? 1);
  const child = spawn(require('electron'), [__dirname, ...process.argv.slice(2)], {
    cwd: __dirname, env: environment, stdio: 'inherit', windowsHide: false
  });
  child.on('exit', code => process.exit(code ?? 1));
  child.on('error', error => { console.error(error.message); process.exit(1); });
});
