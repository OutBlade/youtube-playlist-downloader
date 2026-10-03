const form = document.querySelector('#download-form');
const input = document.querySelector('#playlist-url');
const start = document.querySelector('#start-download');
const message = document.querySelector('#form-message');
const stateLabel = document.querySelector('#download-state');
const panel = document.querySelector('#job-panel');
const empty = document.querySelector('#empty-state');
const progress = document.querySelector('#job-progress');
const archive = document.querySelector('#save-archive');
const files = document.querySelector('#file-list');
const sheet = document.querySelector('#item-sheet');
const sleeve = document.querySelector('#sleeve');
const cover = document.querySelector('#sleeve-cover');
// A static copy of this page finds the running server through this address file.
const BACKEND_INDEX = 'https://raw.githubusercontent.com/OutBlade/youtube-playlist-downloader/backend/backend.json';
const SAVE_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 4v12m-5-5 5 5 5-5M5 19h14"/></svg>';
const frames = new Map();
let base = '';
let ready = false;
let active = false;
let polling = false;

function notice(text, error = false) {
  message.textContent = text;
  message.classList.toggle('error', error);
}
async function api(path, options = {}) {
  const response = await fetch(base + path, {...options, signal: AbortSignal.timeout(15000)});
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    const error = new Error(data.error || 'The downloader could not be reached. Please try again.');
    error.status = response.status;
    throw error;
  }
  return data;
}
const link = path => base ? base + path.replace(/^\//, '') : path;
const thumbnail = id => `https://i.ytimg.com/vi/${id}/mqdefault.jpg`;
function duration(seconds) {
  if (!Number.isFinite(seconds)) return '';
  const total = Math.round(seconds);
  const minutes = Math.floor(total / 60) % 60;
  const rest = String(total % 60).padStart(2, '0');
  return total >= 3600 ? `${Math.floor(total / 3600)}:${String(minutes).padStart(2, '0')}:${rest}` : `${minutes}:${rest}`;
}
function lock(value) {
  active = value;
  start.disabled = !ready || value;
  input.disabled = value;
  form.querySelectorAll('input[name=mode]').forEach(control => control.disabled = value);
  start.querySelector('span').textContent = value ? 'Downloading…' : 'Download';
}
function element(tag, className, text) {
  const node = document.createElement(tag);
  node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
function buildSheet(job) {
  sheet.replaceChildren();
  cover.replaceChildren();
  frames.clear();
  sheet.dataset.job = job.id;
  job.items.forEach((item, position) => {
    const row = element('li', 'frame');
    const shot = element('a', 'frame-shot');
    const image = new Image(320, 180);
    image.alt = '';
    image.loading = 'lazy';
    image.decoding = 'async';
    image.addEventListener('error', () => image.remove());
    image.src = thumbnail(item.id);
    const save = element('span', 'frame-save');
    save.innerHTML = SAVE_ICON;
    const bar = element('progress', 'frame-progress');
    bar.max = 100;
    bar.setAttribute('aria-label', `Progress for ${item.title || 'this item'}`);
    shot.append(image, save, bar);
    const meta = element('div', 'frame-meta');
    const status = element('span', 'frame-status');
    meta.append(element('span', 'frame-index', String(position + 1).padStart(2, '0')), status,
      element('span', 'frame-title', item.title || 'Untitled'));
    row.append(shot, meta);
    sheet.append(row);
    frames.set(item.id, {row, shot, bar, status});
  });
  const art = job.items.length >= 4 ? job.items.slice(0, 4) : job.items.slice(0, 1);
  cover.classList.toggle('single', art.length === 1);
  for (const item of art) {
    const image = new Image(320, 180);
    image.alt = '';
    image.addEventListener('error', () => image.remove());
    image.src = thumbnail(item.id);
    image.dataset.id = item.id;
    cover.append(image);
  }
}
function renderItems(job, items) {
  if (sheet.dataset.job !== job.id || frames.size !== items.length) buildSheet({...job, items});
  for (const item of items) {
    const frame = frames.get(item.id);
    if (!frame) continue;
    frame.row.className = `frame is-${item.state}`;
    const file = job.files[item.file];
    if (file) {
      frame.shot.href = link(file.url);
      frame.shot.download = file.name;
      frame.shot.setAttribute('aria-label', `Save ${item.title || file.name}`);
    } else {
      frame.shot.removeAttribute('href');
      frame.shot.removeAttribute('aria-label');
    }
    frame.bar.hidden = item.state !== 'active';
    if (item.state === 'active' && Number.isFinite(item.percent)) frame.bar.value = item.percent;
    else frame.bar.removeAttribute('value');
    frame.status.textContent = item.state === 'active' ? 'Saving…' : item.note || duration(item.duration);
    const art = cover.querySelector(`[data-id="${item.id}"]`);
    if (art) art.classList.toggle('is-done', item.state === 'done');
  }
}
function clearJob() {
  try { sessionStorage.removeItem('blade-job'); } catch {}
  panel.hidden = true;
  empty.hidden = false;
  stateLabel.textContent = 'No active download';
  sleeve.className = 'sleeve-stage';
  sleeve.style.removeProperty('--out');
  cover.replaceChildren();
  delete sheet.dataset.job;
}
function render(job) {
  panel.hidden = false;
  empty.hidden = true;
  const items = job.items || [];
  job.files = job.files || [];
  const complete = job.state === 'complete' || job.state === 'partial';
  const failed = job.state === 'failed';
  const settled = items.filter(item => item.state !== 'queued' && item.state !== 'active').length;
  const saved = complete ? job.files.length : items.filter(item => item.state === 'done').length;
  const current = items.find(item => item.state === 'active');
  const percentages = [...(job.log || '').matchAll(/\[download\]\s+(\d+(?:\.\d+)?)%/g)];
  const percent = current && Number.isFinite(current.percent) ? current.percent :
    percentages.length ? Number(percentages.at(-1)[1]) : null;
  const fraction = items.length ? Math.min(1, (settled + (percent || 0) / 100) / items.length) : null;

  stateLabel.textContent = complete ? 'Ready to save' : failed ? 'Download failed' : 'In progress';
  document.querySelector('#job-title').textContent = failed ? 'Couldn’t download this playlist' :
    job.title || (items.length === 1 && items[0].title) || (complete ? 'Your playlist is ready' :
    job.state === 'reading' ? 'Reading your playlist' : 'Downloading your playlist');
  const item = [...(job.log || '').matchAll(/Downloading item (\d+) of (\d+)/g)].at(-1);
  document.querySelector('#job-detail').textContent =
    job.state === 'packing' ? 'Preparing your ZIP…' :
    items.length ? `${saved} of ${items.length} saved` :
    complete ? `${job.files.length} file${job.files.length === 1 ? '' : 's'}` :
    item ? `Item ${item[1]} of ${item[2]}${percent === null ? '' : ` · current file ${Math.round(percent)}%`}` : 'Getting started…';
  progress.hidden = complete || failed;
  if (job.state === 'downloading' && fraction !== null) progress.value = fraction * 100;
  else if (job.state === 'downloading' && percent !== null) progress.value = percent;
  else progress.removeAttribute('value');
  document.querySelector('#job-message').textContent = job.message || 'Keep this page open while your files are prepared.';
  document.querySelector('#job-log').textContent = job.log || 'Reading your playlist…';
  archive.hidden = !complete;
  if (complete) archive.href = link(job.archive);

  renderItems(job, items);
  sheet.hidden = !items.length || failed;
  const shown = new Set(items.map(entry => entry.file).filter(index => index !== undefined));
  files.replaceChildren();
  job.files.forEach((file, index) => {
    if (shown.has(index)) return;
    const row = document.createElement('li');
    const anchor = document.createElement('a');
    anchor.href = link(file.url);
    anchor.download = file.name;
    anchor.textContent = file.name;
    row.append(anchor, element('span', '', `${(file.bytes / 1048576).toFixed(1)} MB`));
    files.append(row);
  });

  sleeve.className = `sleeve-stage${failed ? '' : ' is-playing'}${complete ? ' is-complete' : ''}${items.length && !failed ? ' has-cover' : ''}`;
  sleeve.style.setProperty('--out', failed ? 0.22 : complete ? 1 : (0.3 + 0.7 * (fraction || 0)).toFixed(3));
  lock(!complete && !failed);
  return complete || failed;
}
async function poll(id) {
  if (polling) return;
  polling = true;
  let failures = 0;
  while (true) {
    try {
      const job = await api(`api/jobs/${id}`);
      failures = 0;
      if (!render(job)) notice('Download in progress.');
      else { notice(job.state === 'failed' ? 'Check your link, then try again.' : 'Ready. Save your ZIP or download individual files.'); break; }
    } catch (error) {
      if (error.status === 404 || error.status === 410) {
        clearJob(); lock(false);
        notice('Ready when you are.');
        break;
      }
      failures++;
      notice(`${error.message} ${failures < 3 ? 'Reconnecting…' : 'Refresh this page to check your download.'}`, true);
      if (failures >= 3) { lock(false); break; }
    }
    await new Promise(resolve => setTimeout(resolve, 1500));
  }
  polling = false;
}
form.addEventListener('submit', async event => {
  event.preventDefault();
  if (!ready || active) return;
  const url = input.value.trim();
  let valid = false;
  try {
    const parsed = new URL(url);
    valid = ['http:', 'https:'].includes(parsed.protocol) &&
      ['youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com', 'youtu.be'].includes(parsed.hostname);
  } catch {}
  input.setAttribute('aria-invalid', String(!valid));
  if (!valid) { notice('Paste a valid YouTube playlist or video link.', true); input.focus(); return; }
  notice('Starting your download…');
  lock(true);
  try {
    const mode = new FormData(form).get('mode') || form.querySelector('input[name=mode]:checked').value;
    const job = await api('api/jobs', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({url, mode})});
    try { sessionStorage.setItem('blade-job', job.id); } catch {}
    render({...job, files:[], items:[], log:''});
    await poll(job.id);
  } catch (error) { lock(false); notice(error.message, true); }
});
input.addEventListener('input', () => input.removeAttribute('aria-invalid'));
async function connect() {
  try { return (await api('api/health')).ready === true; } catch {}
  const response = await fetch(`${BACKEND_INDEX}?${Date.now()}`, {cache: 'no-store', signal: AbortSignal.timeout(10000)});
  const address = (await response.json()).url;
  if (!/^https:\/\/[a-z0-9.-]+$/.test(address)) throw new Error('No server address');
  base = `${address}/`;
  return (await api('api/health')).ready === true;
}
async function initialize() {
  try {
    ready = await connect();
    notice(ready ? 'Ready when you are.' : 'Downloads are temporarily unavailable. Please try again later.');
  } catch {
    base = '';
    ready = false;
    notice('The downloader is offline right now. This page reconnects on its own.');
    setTimeout(initialize, 30000);
  }
  lock(false);
  try {
    const id = sessionStorage.getItem('blade-job');
    if (ready && /^[a-f0-9]{32}$/.test(id || '')) await poll(id);
  } catch {}
}
initialize();
