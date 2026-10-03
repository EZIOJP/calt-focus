
const CATEGORY_COLORS = {
  'Dev / Docs': '#00ff88',
  'Research': '#44ffcc',
  'Coursework': '#88ffaa',
  'Knowledge Work': '#aaffcc',
  'Design': '#44aaff',
  'Admin / Email': '#aaaaff',
  'Communication': '#8888ff',
  'News': '#ffcc44',
  'Browsing': '#ff8844',
  'Music': '#ff44ff',
  'Shopping': '#ffaa44',
  'Social Media': '#ff4444',
  'Video / Streaming': '#ff6644',
  'Gaming': '#ff8800',
  'Idle / New Tab': '#333350',
  'Unknown': '#444460'
};

const PRODUCTIVE_CATS = ['Dev / Docs', 'Research', 'Coursework', 'Knowledge Work', 'Design'];
const LEISURE_CATS = ['Social Media', 'Video / Streaming', 'Gaming', 'Shopping', 'Music', 'News'];

let allLog = [];
let behavioralLog = [];
let activeFilter = 'All';
let charts = {};

function fmt(sec) { return Math.round(sec / 60); }
function fmtH(sec) { return (sec / 3600).toFixed(1); }
function fmtTime(ts) {
  return new Date(ts).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
}

function load() {
  chrome.storage.local.get(['dailyLog', 'behavioralLog', 'tabSwitchCount'], (result) => {
    allLog = (result.dailyLog || []).slice().reverse();
    behavioralLog = result.behavioralLog || [];
    const switchCount = result.tabSwitchCount || 0;
    render(allLog, behavioralLog, switchCount);
  });
}

function render(log, bLog, switchCount) {
  const totalSec = log.reduce((s, e) => s + e.duration_seconds, 0);
  const prodSec = log.filter(e => PRODUCTIVE_CATS.includes(e.category)).reduce((s, e) => s + e.duration_seconds, 0);
  const leiSec = log.filter(e => LEISURE_CATS.includes(e.category)).reduce((s, e) => s + e.duration_seconds, 0);

  // Hero stats
  document.getElementById('h-sessions').textContent = log.length;
  document.getElementById('h-tracked').textContent = fmtH(totalSec) + 'h';
  document.getElementById('h-prod').textContent = fmt(prodSec) + 'm';
  document.getElementById('h-leisure').textContent = fmt(leiSec) + 'm';
  document.getElementById('h-switches').textContent = switchCount;

  // Insight
  if (log.length > 5) {
    const prodPct = totalSec > 0 ? Math.round((prodSec / totalSec) * 100) : 0;
    const leiPct = totalSec > 0 ? Math.round((leiSec / totalSec) * 100) : 0;
    let insight = '';

    if (prodPct > 60) insight = `You're in a <strong>highly productive</strong> session — ${prodPct}% of your time has been productive. Keep it up!`;
    else if (leiPct > 50) insight = `<strong>${leiPct}% leisure</strong> time detected. Consider setting a focus timer to get back on track.`;
    else insight = `Balanced session: <strong>${prodPct}% productive</strong>, ${leiPct}% leisure, ${100-prodPct-leiPct}% other. ${switchCount} tab switches recorded.`;

    document.getElementById('insightText').innerHTML = insight;
    document.getElementById('insightBox').style.display = 'block';
  }

  // Category totals
  const catTotals = {};
  log.forEach(e => { catTotals[e.category] = (catTotals[e.category] || 0) + e.duration_seconds; });
  const sortedCats = Object.entries(catTotals).sort((a, b) => b[1] - a[1]);

  // PIE / BAR / TIMELINE — Chart.js is optional (MV3 cannot load the CDN).
  if (typeof Chart !== "undefined") {
  // PIE CHART
  if (charts.pie) charts.pie.destroy();
  const pieCtx = document.getElementById('categoryPie').getContext('2d');
  charts.pie = new Chart(pieCtx, {
    type: 'doughnut',
    data: {
      labels: sortedCats.map(([c]) => c),
      datasets: [{
        data: sortedCats.map(([, s]) => s),
        backgroundColor: sortedCats.map(([c]) => CATEGORY_COLORS[c] || '#444460'),
        borderWidth: 0,
        hoverOffset: 4
      }]
    },
    options: {
      cutout: '65%',
      plugins: {
        legend: {
          position: 'bottom',
          labels: { color: '#888899', font: { family: 'JetBrains Mono', size: 10 }, padding: 12 }
        },
        tooltip: {
          callbacks: {
            label: (ctx) => `${ctx.label}: ${fmt(ctx.raw)}m`
          }
        }
      }
    }
  });

  // BAR CHART
  if (charts.bar) charts.bar.destroy();
  const barCtx = document.getElementById('prodLeisureBar').getContext('2d');
  charts.bar = new Chart(barCtx, {
    type: 'bar',
    data: {
      labels: sortedCats.slice(0, 8).map(([c]) => c.length > 12 ? c.slice(0,12)+'…' : c),
      datasets: [{
        data: sortedCats.slice(0, 8).map(([, s]) => fmt(s)),
        backgroundColor: sortedCats.slice(0, 8).map(([c]) => (CATEGORY_COLORS[c] || '#444460') + '99'),
        borderColor: sortedCats.slice(0, 8).map(([c]) => CATEGORY_COLORS[c] || '#444460'),
        borderWidth: 1,
        borderRadius: 4
      }]
    },
    options: {
      indexAxis: 'y',
      plugins: { legend: { display: false } },
      scales: {
        x: { ticks: { color: '#555570', font: { family: 'JetBrains Mono', size: 9 } }, grid: { color: '#1a1a30' } },
        y: { ticks: { color: '#888899', font: { family: 'JetBrains Mono', size: 9 } }, grid: { display: false } }
      }
    }
  });

  // TIMELINE CHART (sessions over time in hourly buckets)
  const buckets = {};
  for (let h = 0; h < 24; h++) buckets[h] = 0;
  log.forEach(e => {
    const hour = new Date(e.timestamp).getHours();
    buckets[hour] = (buckets[hour] || 0) + e.duration_seconds;
  });

  if (charts.timeline) charts.timeline.destroy();
  const tlCtx = document.getElementById('timelineChart').getContext('2d');
  charts.timeline = new Chart(tlCtx, {
    type: 'bar',
    data: {
      labels: Object.keys(buckets).map(h => `${h}:00`),
      datasets: [{
        label: 'Time Online (min)',
        data: Object.values(buckets).map(s => fmt(s)),
        backgroundColor: 'rgba(68,136,255,0.4)',
        borderColor: '#4488ff',
        borderWidth: 1,
        borderRadius: 3
      }]
    },
    options: {
      plugins: { legend: { display: false } },
      scales: {
        x: { ticks: { color: '#555570', font: { family: 'JetBrains Mono', size: 9 }, maxRotation: 0 }, grid: { color: '#0d0d1a' } },
        y: { ticks: { color: '#555570', font: { family: 'JetBrains Mono', size: 9 } }, grid: { color: '#1a1a30' } }
      }
    }
  });
  }

  // BEHAVIORAL STATS
  if (bLog.length > 0) {
    const modes = {};
    let totalScroll = 0, totalKeys = 0, totalClicks = 0, idleCount = 0, writingCount = 0;
    bLog.forEach(b => {
      modes[b.interaction_mode] = (modes[b.interaction_mode] || 0) + 1;
      totalScroll += b.scroll_depth_percent || 0;
      totalKeys += b.keystrokes || 0;
      totalClicks += b.mouse_clicks || 0;
      if (b.is_idle) idleCount++;
      if (b.interaction_mode === 'writing') writingCount++;
    });

    const topMode = Object.entries(modes).sort((a, b) => b[1] - a[1])[0]?.[0] || '—';
    document.getElementById('b-mode').textContent = topMode;
    document.getElementById('b-scroll').textContent = Math.round(totalScroll / bLog.length) + '%';
    document.getElementById('b-keystrokes').textContent = totalKeys.toLocaleString();
    document.getElementById('b-clicks').textContent = totalClicks.toLocaleString();
    document.getElementById('b-idle').textContent = idleCount;
    document.getElementById('b-writing').textContent = writingCount;
  }

  // FILTER BUTTONS
  const cats = ['All', ...new Set(log.map(e => e.category))];
  document.getElementById('filterRow').innerHTML = cats.map(c =>
    `<button class="filter-btn ${c === activeFilter ? 'active' : ''}" data-cat="${c}">${c}</button>`
  ).join('');
  document.querySelectorAll('.filter-btn').forEach(b => {
    b.addEventListener('click', () => {
      activeFilter = b.dataset.cat;
      renderTable(log);
      document.querySelectorAll('.filter-btn').forEach(x => x.classList.remove('active'));
      b.classList.add('active');
    });
  });

  renderTable(log);
}

function renderTable(log) {
  const filtered = activeFilter === 'All' ? log : log.filter(e => e.category === activeFilter);
  document.getElementById('sessionCount').textContent = `${filtered.length} sessions`;

  if (filtered.length === 0) {
    document.getElementById('tableWrap').innerHTML = '<div class="empty-state">No sessions yet — start browsing!</div>';
    return;
  }

  const rows = filtered.slice(0, 200).map(e => {
    const color = CATEGORY_COLORS[e.category] || '#444460';
    const pct = e.productivity_score || 0;
    const barW = Math.max(4, pct);

    return `<tr>
      <td>${fmtTime(e.timestamp)}</td>
      <td class="url-cell" title="${e.url}">${e.domain || e.url}</td>
      <td style="max-width:180px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#888899" title="${e.title}">${e.title}</td>
      <td><span class="badge" style="background:${color}22;color:${color};border:1px solid ${color}44">${e.category}</span></td>
      <td>${e.duration_seconds}s</td>
      <td>
        <div class="score-bar">
          <div class="score-mini" style="width:${barW}px;background:${pct>60?'#00ff88':pct>30?'#ffcc44':'#ff4444'}"></div>
          <span style="font-size:10px;color:${pct>60?'#00ff88':pct>30?'#ffcc44':'#ff4444'}">${pct}</span>
        </div>
      </td>
      <td style="color:#555570">${e.reason || '—'}</td>
    </tr>`;
  }).join('');

  document.getElementById('tableWrap').innerHTML = `
    <table>
      <thead>
        <tr>
          <th>Time</th>
          <th>Domain</th>
          <th>Title</th>
          <th>Category</th>
          <th>Duration</th>
          <th>Score</th>
          <th>Reason</th>
        </tr>
      </thead>
      <tbody>${rows}</tbody>
    </table>
  `;
}

// EXPORT CSV
document.getElementById('exportBtn').addEventListener('click', () => {
  chrome.storage.local.get(['dailyLog'], (result) => {
    const log = result.dailyLog || [];
    if (!log.length) { alert('No data to export.'); return; }

    const headers = ['timestamp', 'end_timestamp', 'duration_seconds', 'url', 'domain', 'title', 'category', 'productivity_score', 'reason', 'tab_switches_today'];
    const rows = log.map(e => headers.map(h => `"${(e[h] ?? '').toString().replace(/"/g, '""')}"`).join(','));
    const csv = [headers.join(','), ...rows].join('\n');

    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `DSC_browser_behavior_${new Date().toISOString().slice(0,10)}.csv`;
    a.click();
  });
});

document.getElementById('clearBtn').addEventListener('click', () => {
  if (!confirm('Clear all tracking data? This cannot be undone.')) return;
  chrome.runtime.sendMessage({ type: 'CLEAR_DATA' });
  setTimeout(load, 300);
});

document.getElementById('refreshBtn').addEventListener('click', load);

// Auto-refresh every 10s
load();
setInterval(load, 10000);
