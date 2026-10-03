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
let ready = false;
let active = false;
let polling = false;

function notice(text, error = false) {
  message.textContent = text;
  message.classList.toggle('error', error);
}
async function api(path, options = {}) {
  const response = await fetch(path, {...options, signal: AbortSignal.timeout(15000)});
  const data = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(data.error || 'The downloader could not be reached. Please try again.');
  return data;
}
function lock(value) {
  active = value;
  start.disabled = !ready || value;
  input.disabled = value;
  form.querySelectorAll('input[name=mode]').forEach(control => control.disabled = value);
  start.querySelector('span').textContent = value ? 'Downloading…' : 'Download';
}
function render(job) {
  panel.hidden = false;
  empty.hidden = true;
  const complete = job.state === 'complete' || job.state === 'partial';
  const failed = job.state === 'failed';
  stateLabel.textContent = complete ? 'Ready to save' : failed ? 'Download failed' : 'In progress';
  document.querySelector('#job-title').textContent = complete ? 'Your playlist is ready' :
    failed ? 'Couldn’t download this playlist' : job.state === 'packing' ? 'Preparing your ZIP' : 'Downloading your playlist';
  const percentages = [...(job.log || '').matchAll(/\[download\]\s+(\d+(?:\.\d+)?)%/g)];
  const percent = percentages.length ? Number(percentages.at(-1)[1]) : null;
  progress.hidden = complete || failed;
  if (percent !== null && job.state === 'downloading') progress.value = percent;
  else progress.removeAttribute('value');
  const item = [...(job.log || '').matchAll(/Downloading item (\d+) of (\d+)/g)].at(-1);
  document.querySelector('#job-detail').textContent = complete ? `${job.files.length} file${job.files.length === 1 ? '' : 's'}` :
    item ? `Item ${item[1]} of ${item[2]}${percent === null ? '' : ` · current file ${Math.round(percent)}%`}` :
    job.state === 'packing' ? 'Nearly there…' : 'Getting started…';
  document.querySelector('#job-message').textContent = job.message || 'Keep this page open while your files are prepared.';
  document.querySelector('#job-log').textContent = job.log || 'Reading your playlist…';
  archive.hidden = !complete;
  if (complete) archive.href = job.archive;
  files.replaceChildren();
  for (const file of job.files || []) {
    const row = document.createElement('li');
    const link = document.createElement('a');
    link.href = file.url;
    link.download = file.name;
    link.textContent = file.name;
    const size = document.createElement('span');
    size.textContent = `${(file.bytes / 1048576).toFixed(1)} MB`;
    row.append(link, size);
    files.append(row);
  }
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
      if (render(job)) { notice(job.state === 'failed' ? 'Check your link, then try again.' : 'Ready. Save your ZIP or download individual files.'); break; }
    } catch (error) {
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
    render({...job, files:[], log:''});
    await poll(job.id);
  } catch (error) { lock(false); notice(error.message, true); }
});
input.addEventListener('input', () => input.removeAttribute('aria-invalid'));
async function initialize() {
  try {
    const health = await api('api/health');
    ready = health.ready === true;
    notice(ready ? 'Ready when you are.' : 'Downloads are temporarily unavailable. Please try again later.');
  } catch {
    notice('Website preview — downloads will be available after server deployment.');
  }
  lock(false);
  try {
    const id = sessionStorage.getItem('blade-job');
    if (ready && /^[a-f0-9]{32}$/.test(id || '')) await poll(id);
  } catch {}
}
initialize();
