import fs from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import path from 'node:path';
import { pipeline } from 'node:stream/promises';
import { Transform } from 'node:stream';
import { crc32 } from 'node:zlib';
import yauzl from 'yauzl';

export function archivePath(directory, name) {
  if (/[\\:<>"|?*\x00-\x1f]/.test(name) || name.startsWith('/') || name.split('/').some(p => p === '..' || /[. ]$/.test(p) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(p))) throw new Error('Unsafe archive path');
  const target = path.resolve(directory, name);
  if (!target.startsWith(path.resolve(directory) + path.sep)) throw new Error('Unsafe archive path');
  return target;
}

export async function zipSize(file, directory, { signal, limit = 256 * 1024 ** 3 } = {}) {
  const zip = await new Promise((resolve, reject) => yauzl.open(file, { lazyEntries: true }, (err, result) => err ? reject(err) : resolve(result)));
  return new Promise((resolve, reject) => {
    let total = 0, count = 0;
    const names = new Set();
    const fail = error => { zip.close(); reject(error); };
    zip.on('error', fail);
    zip.on('entry', entry => {
      try {
        signal?.throwIfAborted();
        const name = archivePath(directory, entry.fileName).toLowerCase();
        if (names.has(name) || (entry.externalFileAttributes >>> 16 & 0xf000) === 0xa000 || entry.generalPurposeBitFlag & 1) throw new Error('ZIP contains duplicate, linked or encrypted files.');
        names.add(name); total += entry.uncompressedSize;
        if (++count > 200000 || !Number.isSafeInteger(total) || total > limit) throw new Error('ZIP exceeds extraction limit.');
        zip.readEntry();
      } catch (error) { fail(error); }
    });
    zip.on('end', () => resolve(total));
    zip.readEntry();
  });
}

export async function extractZip(file, directory, { signal, progress = () => {}, limit = 16 * 1024 ** 3 } = {}) {
  await fs.mkdir(directory, { recursive: true });
  const zip = await new Promise((resolve, reject) => yauzl.open(file, { lazyEntries: true }, (err, result) => err ? reject(err) : resolve(result)));
  let total = 0, done = 0, active = false, settled = false;
  return new Promise((resolve, reject) => {
    const abort = () => { if (!active) fail(new Error('Cancelled')); };
    const fail = error => { if (settled) return; settled = true; signal?.removeEventListener('abort', abort); zip.close(); reject(error); };
    signal?.addEventListener('abort', abort, { once: true });
    zip.on('error', fail);
    zip.on('end', () => { settled = true; signal?.removeEventListener('abort', abort); resolve(); });
    zip.on('entry', async entry => {
      active = true;
      try {
        signal?.throwIfAborted();
        const target = archivePath(directory, entry.fileName);
        const mode = entry.externalFileAttributes >>> 16;
        if ((mode & 0xf000) === 0xa000) throw new Error('Archive contains a symbolic link');
        total += entry.uncompressedSize;
        if (total > limit) throw new Error('Archive exceeds extraction limit');
        if (entry.fileName.endsWith('/')) await fs.mkdir(target, { recursive: true });
        else {
          await fs.mkdir(path.dirname(target), { recursive: true });
          const stream = await new Promise((yes, no) => zip.openReadStream(entry, (e, s) => e ? no(e) : yes(s)));
          let checksum = 0;
          const verify = new Transform({ transform(chunk, _encoding, callback) { checksum = crc32(chunk, checksum); callback(null, chunk); } });
          await pipeline(stream, verify, createWriteStream(target, { flags: 'wx' }), { signal });
          if (checksum !== entry.crc32) throw new Error(`ZIP checksum failed: ${entry.fileName}`);
        }
        signal?.throwIfAborted();
        progress(++done, zip.entryCount); active = false; zip.readEntry();
      } catch (error) { active = false; fail(error); }
    });
    if (signal?.aborted) abort(); else zip.readEntry();
  });
}
