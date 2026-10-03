const $ = s => document.querySelector(s);
const api = async (u, o) => (await fetch(u, o)).json();
const post = (u, d) => api(u, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(d) });
let bikes = [];

function showBike(b) {
  const ok = b.e20;
  $('#result').innerHTML = `<div class="res ${ok ? '' : 'no'}"><h3>${ok ? '✅ E20 Compatible' : '❌ E20 Not Confirmed'}</h3>
  <div class="meta"><span><b>${b.name}</b> · ${b.cc}cc · ${b.category}</span><span>Production Period: ${b.year_from} - Present</span><span>Source: ${b.source}</span></div>
  <div class="note"><b>Note:</b><br>${b.note}. Expected mileage ${b.kmpl} km/l · service every ${b.service_interval} km.</div></div>`;
}
const opts = (el, list, f) => el.innerHTML = list.map(f).join('');

async function loadModels() {
  const list = await api('/api/bikes?brand=' + encodeURIComponent($('#brand').value));
  opts($('#model'), list, b => `<option value="${b.id}">${b.model}</option>`);
  showBike(list[0]); $('#model').onchange = async () => showBike(await api('/api/bike/' + $('#model').value));
}

async function search() {
  const list = await api('/api/search?q=' + encodeURIComponent($('#q').value));
  $('#sugg').innerHTML = list.map(b => `<div data-id="${b.id}">${b.name} — ${b.e20 ? '✅' : '❌'}</div>`).join('');
  if (list.length) showBike(list[0]);
  $('#sugg').querySelectorAll('div').forEach(d => d.onclick = async () => showBike(await api('/api/bike/' + d.dataset.id)));
}

function drawChart(data, expected) {
  const c = $('#chart'), g = c.getContext('2d'), W = c.width, H = c.height, P = 50;
  g.clearRect(0, 0, W, H); $('#hEmpty').textContent = data.length ? '' : 'No mileage logged yet for this bike.';
  if (!data.length) return;
  const vals = data.map(d => d.kmpl), max = Math.max(...vals, expected) * 1.2, min = 0;
  const X = i => P + (data.length === 1 ? (W - 2 * P) / 2 : i * (W - 2 * P) / (data.length - 1)), Y = v => H - P - (v - min) / (max - min) * (H - 2 * P);
  g.strokeStyle = '#2f3133'; g.fillStyle = '#b5b8bd'; g.font = '12px sans-serif';
  for (let i = 0; i <= 4; i++) { const v = max * i / 4; g.beginPath(); g.moveTo(P, Y(v)); g.lineTo(W - P, Y(v)); g.stroke(); g.fillText(v.toFixed(0), 12, Y(v) + 4); }
  g.setLineDash([6, 6]); g.strokeStyle = '#f59e0b'; g.beginPath(); g.moveTo(P, Y(expected)); g.lineTo(W - P, Y(expected)); g.stroke(); g.setLineDash([]);
  g.fillStyle = '#f59e0b'; g.fillText('Expected ' + expected + ' km/l', W - P - 110, Y(expected) - 6);
  g.strokeStyle = '#33b8c9'; g.lineWidth = 3; g.beginPath();
  data.forEach((d, i) => i ? g.lineTo(X(i), Y(d.kmpl)) : g.moveTo(X(i), Y(d.kmpl))); g.stroke();
  data.forEach((d, i) => { g.fillStyle = '#33b8c9'; g.beginPath(); g.arc(X(i), Y(d.kmpl), 5, 0, 7); g.fill(); g.fillStyle = '#f4f4f5'; g.fillText(d.kmpl.toFixed(1), X(i) - 10, Y(d.kmpl) - 12); });
}
async function refreshChart() {
  const id = $('#mBike').value, b = bikes.find(x => x.id == id);
  drawChart(await api('/api/mileage/' + id), b.kmpl);
}

async function init() {
  const st = await api('/api/stats');
  $('#sBrands').textContent = st.brands; $('#sModels').textContent = $('#sModels2').textContent = st.models; $('#sPct').textContent = st.compatible_pct + '%';
  opts($('#brand'), await api('/api/brands'), b => `<option>${b}</option>`);
  $('#brand').onchange = loadModels; await loadModels();
  bikes = await api('/api/bikes');
  const all = b => `<option value="${b.id}">${b.name}</option>`;
  opts($('#mBike'), bikes, all); opts($('#sBike'), bikes, all);
  $('#mBike').onchange = refreshChart; refreshChart();
  $('#qBtn').onclick = search; $('#q').onkeydown = e => e.key === 'Enter' && search();

  $('#mBtn').onclick = async () => {
    const d = +$('#dist').value, f = +$('#fuel').value;
    if (d <= 0 || f <= 0) return $('#mRes').innerHTML = '<div class="res no">Enter valid distance and fuel.</div>';
    const r = await post('/api/mileage', { bike_id: +$('#mBike').value, distance: d, fuel: f, price: +$('#price').value || 0 });
    const good = r.kmpl >= r.expected * 0.9;
    $('#mRes').innerHTML = `<div class="res ${good ? '' : 'warn'}"><h3>${r.kmpl.toFixed(2)} km/l</h3>Expected ${r.expected} km/l${r.cost_per_km ? ' · ₹' + r.cost_per_km.toFixed(2) + '/km' : ''}</div>`;
    refreshChart();
  };
  $('#sBtn').onclick = async () => {
    const r = await post('/api/maintenance', { bike_id: +$('#sBike').value, last_service_km: +$('#last').value, current_km: +$('#cur').value });
    const cls = r.status === 'OK' ? '' : r.status === 'Overdue' ? 'no' : 'warn';
    $('#sRes').innerHTML = `<div class="res ${cls}"><h3>${r.status === 'OK' ? '✅' : '🔔'} ${r.status}</h3>Service interval: ${r.interval} km · Used: ${r.used} km<br>${r.remaining >= 0 ? r.remaining + ' km left' : Math.abs(r.remaining) + ' km overdue'} · Next service at ${r.next_service_at} km</div>`;
  };
}
init();
