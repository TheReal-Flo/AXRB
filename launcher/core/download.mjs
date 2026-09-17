import fs from 'node:fs/promises';
import path from 'node:path';
import { createHash } from 'node:crypto';

export function safeName(value) {
  if (typeof value !== 'string' || !value || value.length > 220 || /[<>:"/\\|?*\x00-\x1f]/.test(value) || /[. ]$/.test(value) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value))
    throw new Error('Meta returned an unsafe asset filename.');
  return value;
}
export function allowedDownload(value) {
  const url = new URL(value);
  const trusted = ['oculus.com', 'oculuscdn.com', 'facebook.com', 'fbcdn.net', 'meta.com', 'fbsbx.com'];
  if (url.protocol !== 'https:' || url.username || url.password || (url.port && url.port !== '443') ||
    !trusted.some(d => url.hostname === d || url.hostname.endsWith(`.${d}`))) throw new Error('Download URL is not on a Meta delivery domain.');
  return url;
}
async function fetchFile(url, options, request, validate) {
  for (let redirects = 0; redirects < 8; redirects++) {
    validate(url);
    const response = await request(url, { ...options, redirect: 'manual' });
    if ([301, 302, 303, 307, 308].includes(response.status)) {
      const location = response.headers.get('location');
      await response.body?.cancel();
      if (!location) throw new Error('Download redirect has no location.');
      url = new URL(location, url).href;
      continue;
    }
    return response;
  }
  throw new Error('Too many download redirects.');
}

// Only rename completed files. Range resume is conditional on a stable ETag.
export async function downloadFile({ url, destination, size = 0, signal, progress = () => {}, request = fetch, validate = allowedDownload }) {
  validate(url);
  if (!Number.isSafeInteger(size) || size < 0) throw new Error('Invalid download size.');
  await fs.mkdir(path.dirname(destination), { recursive: true });
  const part = `${destination}.part`, sidecar = `${part}.json`;
  let offset = 0, etag = '';
  try {
    const meta = JSON.parse(await fs.readFile(sidecar, 'utf8'));
    const stat = await fs.stat(part);
    if (meta.size === size && meta.etag && !meta.etag.startsWith('W/') && stat.size <= (size || Infinity)) { offset = stat.size; etag = meta.etag; }
  } catch {}
  const headers = offset ? { Range: `bytes=${offset}-`, 'If-Range': etag } : {};
  const response = await fetchFile(url, { headers, signal: AbortSignal.any([signal || new AbortController().signal, AbortSignal.timeout(30 * 60 * 1000)]) }, request, validate);
  if (![200, 206].includes(response.status)) { await response.body?.cancel(); throw new Error(`Meta's file server refused the download (HTTP ${response.status}). Library ownership and file-delivery access are separate; try reconnecting Meta.`); }
  if (/text\/html|application\/json/i.test(response.headers.get('content-type') || '')) { await response.body?.cancel(); throw new Error('Meta returned an error page instead of the requested file.'); }
  if (response.status === 206) {
    const range = response.headers.get('content-range')?.match(/^bytes (\d+)-(\d+)\/(\d+)$/);
    if (!range || Number(range[1]) !== offset || (size && Number(range[3]) !== size)) { await response.body?.cancel(); throw new Error('Server returned an inconsistent download range.'); }
  } else offset = 0;
  const length = Number(response.headers.get('content-length') || 0);
  const total = size || (length ? length + offset : 0);
  if (size && length && length + offset !== size) { await response.body?.cancel(); throw new Error('Download size does not match Meta metadata.'); }
  await fs.writeFile(sidecar, JSON.stringify({ size, etag: response.headers.get('etag') || '' }));
  const file = await fs.open(part, offset ? 'a' : 'w');
  let received = offset;
  try {
    if (!response.body) throw new Error('Download response was empty.');
    for await (const chunk of response.body) {
      signal?.throwIfAborted();
      if (total && received + chunk.length > total) throw new Error('Download exceeded its expected size.');
      let written = 0;
      while (written < chunk.length) written += (await file.write(chunk, written, chunk.length - written)).bytesWritten;
      received += chunk.length; progress(received, total);
    }
    if (!received || (total && received !== total)) throw new Error('Download ended before the complete file arrived.');
    await file.sync();
  } finally { await file.close(); }
  // Locally recorded digest lets installation detect damaged/changed downloads.
  const hash = createHash('sha256');
  const handle = await fs.open(part);
  try { for await (const chunk of handle.createReadStream()) hash.update(chunk); } finally { await handle.close(); }
  await fs.rename(part, destination);
  await fs.rm(sidecar, { force: true });
  return { bytes: received, sha256: hash.digest('hex') };
}

export async function checkSpace(directory, bytes) {
  await fs.mkdir(directory, { recursive: true });
  const space = await fs.statfs(directory);
  const available = Number(space.bavail) * Number(space.bsize);
  const reserve = 5 * 1024 ** 3; // Keep Android's cold-boot minimum available.
  if (available < bytes + reserve) throw new Error(`Not enough disk space. This download needs ${(bytes / 1024 ** 3).toFixed(1)} GB plus 5 GB free for Android. Choose another download folder in Settings.`);
}
