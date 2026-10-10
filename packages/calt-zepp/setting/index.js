/**
 * Phone settings — Base URL + token for manual health dump.
 */
AppSettingsPage({
  build(props) {
    const storage = props.settingsStorage
    const get = (k, d) => {
      const v = storage.getItem(k)
      return v !== undefined && v !== null && v !== '' ? v : d
    }

    let logs = []
    try {
      logs = JSON.parse(get('sync_log_json', '[]'))
    } catch (_) {
      logs = []
    }
    if (!Array.isArray(logs)) logs = []

    const lastOk = get('last_sync_ok', '') === '1'
    const pingOk = get('last_ping_ok', '') === '1'
    const wroteLife = get('last_wrote_life', '') === '1'
    const host = get('last_url_host', '') || get('base_url', '')

    const logLines = logs.slice(0, 10).map((e, i) => {
      const t = (e.at || '').replace('T', ' ').slice(0, 19)
      const flag = e.ok ? 'OK' : 'ERR'
      const detail = [e.summary, e.diag, (e.errors || []).slice(0, 2).join(' | ')]
        .filter(Boolean)
        .join('\n')
      return Text(
        {
          key: `log-${i}`,
          style: {
            fontSize: '11px',
            color: e.ok ? '#1a7f4e' : '#b33',
            marginBottom: '10px',
            lineHeight: '1.4',
            fontFamily: 'ui-monospace, monospace',
            whiteSpace: 'pre-wrap',
          },
        },
        [`${flag} ${t}${e.host ? ` @${e.host}` : ''}\n${detail}`],
      )
    })

    return View(
      {
        style: {
          padding: '12px 16px',
          fontFamily: 'system-ui, sans-serif',
          paddingBottom: '48px',
        },
      },
      [
        Text(
          { style: { fontSize: '18px', fontWeight: '600', marginBottom: '4px' } },
          ['CALT Sync 4.3'],
        ),
        Text(
          {
            style: {
              fontSize: '13px',
              color: '#b45309',
              background: '#fff7ed',
              padding: '10px',
              borderRadius: '8px',
              marginBottom: '14px',
              lineHeight: '1.45',
            },
          },
          [
            'Rich health dump → CALT Focus on the PC LAN (:8765). The enforcer stores it. Dump & Send on the watch, or Auto sync every 3h. Phone and PC on the same Wi-Fi. Sensors do not invent history.',
          ],
        ),

        Text(
          { style: { fontSize: '14px', fontWeight: '600', marginBottom: '8px' } },
          ['Connection'],
        ),
        TextInput({
          label: 'PC address (CALT Focus :8765)',
          value: get('base_url', 'http://192.168.0.110:8765'),
          onChange: (val) => storage.setItem('base_url', String(val || '').trim()),
        }),
        Text(
          {
            style: {
              fontSize: '12px',
              color: '#7f1d1d',
              background: '#fef2f2',
              padding: '10px',
              borderRadius: '8px',
              marginTop: '8px',
              lineHeight: '1.45',
            },
          },
          [
            'Use the PC LAN address. CALT Focus listens on :8765 and the enforcer stores the dump. ' +
              'localhost will not work. Check http://<PC-LAN-IP>:8765/health in the phone browser.',
          ],
        ),
        TextInput({
          label: 'Ingest token',
          value: get('ingest_token', 'calt-local-wearables'),
          onChange: (val) => storage.setItem('ingest_token', val),
        }),

        Text(
          {
            style: {
              fontSize: '14px',
              fontWeight: '600',
              marginTop: '18px',
              marginBottom: '8px',
            },
          },
          ['Last result'],
        ),
        Text(
          {
            style: {
              fontSize: '12px',
              color: lastOk ? '#1a7f4e' : '#666',
              lineHeight: '1.45',
              marginBottom: '8px',
              padding: '10px',
              background: '#f5f5f5',
              borderRadius: '8px',
              whiteSpace: 'pre-wrap',
            },
          },
          [
            `Host: ${host || '—'}\n` +
              `When: ${get('last_sync_at', '—')}\n` +
              `PC received: ${get('last_received_at', '') || 'never'}\n` +
              `OK: ${get('last_sync_ok', '') === '' ? 'none' : lastOk ? 'yes' : 'no'}\n` +
              `Life Tracker write: ${wroteLife ? 'yes' : 'no'}\n` +
              `Steps: ${get('last_steps', '—')} · Sleep min: ${get('last_sleep_min', '—')}\n` +
              `Summary: ${get('last_sync_summary', '')}\n` +
              `Diag: ${get('last_diag', get('last_ping_detail', ''))}\n` +
              `Errors: ${get('last_sync_errors', '—')}`,
          ],
        ),
        Text(
          {
            style: {
              fontSize: '12px',
              color: pingOk ? '#1a7f4e' : '#666',
              marginBottom: '12px',
            },
          },
          [`Last ping: ${get('last_ping_at', '—')} · ${get('last_ping_detail', '')}`],
        ),

        Text(
          {
            style: {
              fontSize: '14px',
              fontWeight: '600',
              marginBottom: '8px',
            },
          },
          ['Dump log'],
        ),
        ...(logLines.length
          ? logLines
          : [
              Text(
                { style: { fontSize: '12px', color: '#999', marginBottom: '8px' } },
                ['No dumps yet — Dump + Send on the watch.'],
              ),
            ]),

        Button({
          label: 'Clear dump log',
          style: {
            marginTop: '12px',
            fontSize: '13px',
            borderRadius: '8px',
            background: '#eee',
            color: '#333',
          },
          onClick: () => {
            ;[
              'sync_log_json',
              'last_sync_at',
              'last_sync_ok',
              'last_sync_summary',
              'last_sync_errors',
              'last_steps',
              'last_sleep_min',
              'last_wrote_life',
              'last_diag',
              'last_url_host',
              'last_ping_detail',
            ].forEach((k) => storage.setItem(k, k === 'sync_log_json' ? '[]' : ''))
          },
        }),
      ],
    )
  },
})
