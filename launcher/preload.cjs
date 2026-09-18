const { contextBridge, ipcRenderer } = require('electron');
const channels = ['questDevices', 'questGames', 'questImport', 'importZip', 'setupCheck', 'setupStart', 'setupCancel', 'setupFeatures', 'setupLicense', 'state', 'sync', 'login', 'logout', 'search', 'add', 'lookup', 'builds', 'download', 'dlc', 'downloadDlc', 'cancel', 'retry', 'import', 'importAssets', 'install', 'uninstall', 'patch', 'play', 'stop', 'fpsHud', 'settings', 'chooseFolder', 'chooseCli', 'openFolder', 'openStore'];
contextBridge.exposeInMainWorld('axrb', Object.fromEntries([
  ...channels.map(name => [name, (...args) => ipcRenderer.invoke(`axrb:${name}`, ...args)]),
  ['onChange', callback => { const listener = (_event, state) => callback(state); ipcRenderer.on('axrb:changed', listener); return () => ipcRenderer.removeListener('axrb:changed', listener); }],
  ['onLaunchError', callback => { const listener = (_event, error) => callback(error); ipcRenderer.on('axrb:launch-error', listener); return () => ipcRenderer.removeListener('axrb:launch-error', listener); }]
]));
