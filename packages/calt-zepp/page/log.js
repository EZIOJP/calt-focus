/**
 * Detailed sync log — fits round/square watch; shows host + network hints.
 */
import { createWidget, widget, align, text_style, prop } from '@zos/ui'
import { back } from '@zos/router'
import { localStorage } from '@zos/storage'
import { screen, yFrac } from './layout'
import { sidePayload } from '../shared/sidePayload'

function loadLocalLogs() {
  try {
    const raw = localStorage.getItem('calt_sync_log')
    const logs = raw ? JSON.parse(raw) : []
    return Array.isArray(logs) ? logs : []
  } catch (_) {
    return []
  }
}

function formatEntry(e) {
  const t = (e.at || '').replace('T', ' ').slice(11, 19)
  const flag = e.ok ? 'OK' : 'ERR'
  const host = e.host || ''
  // Host on its own line so round-screen crop doesn't invent "+2.168…"
  const hostLine = host ? `host ${host}` : 'host ?'
  const err = (e.errors || [])[0] || ''
  const shortErr = String(err)
    .replace(/^health:\s*/i, '')
    .replace(/^ingest\s*/i, '')
    .slice(0, 70)
  return `${flag} ${t}\n${hostLine}\n${shortErr || e.summary || ''}`
}

Page({
  build() {
    const { messageBuilder } = getApp()._options.globalData
    const S = screen()
    const { pad, contentW, width, height } = S

    createWidget(widget.TEXT, {
      x: pad,
      y: yFrac(height, 0.03),
      w: contentW,
      h: Math.round(height * 0.06),
      color: 0xffffff,
      text_size: Math.round(width * 0.05),
      align_h: align.CENTER_H,
      text: 'Sync logs',
    })

    const metaW = createWidget(widget.TEXT, {
      x: pad,
      y: yFrac(height, 0.1),
      w: contentW,
      h: Math.round(height * 0.16),
      color: 0x88c0bb,
      text_size: Math.round(width * 0.032),
      align_h: align.LEFT,
      text_style: text_style.WRAP,
      text: 'Loading…',
    })

    const bodyW = createWidget(widget.TEXT, {
      x: pad,
      y: yFrac(height, 0.28),
      w: contentW,
      h: Math.round(height * 0.48),
      color: 0xffffff,
      text_size: Math.round(width * 0.03),
      align_h: align.LEFT,
      text_style: text_style.WRAP,
      text: '…',
    })

    const local = loadLocalLogs()
    bodyW.setProperty(
      prop.TEXT,
      local.length
        ? local.slice(0, 5).map(formatEntry).join('\n\n')
        : 'No logs yet.\nRun Test or Sync.',
    )

    messageBuilder
      .request({ method: 'GET_SETTINGS' })
      .then((res) => {
        const s = sidePayload(res)
        const issues = (s.url_issues || []).join('; ')
        // Show full host on its own lines — avoid crop looking like "+2.168…"
        const host = s.host || '(not set)'
        const hostLines =
          host.length > 18 ? `${host.slice(0, 18)}\n${host.slice(18)}` : host
        metaW.setProperty(
          prop.TEXT,
          `Host:\n${hostLines}\n${(s.last_diag || s.last_ping_detail || s.last_sync_summary || '—').slice(0, 90)}${
            issues ? `\n⚠ ${String(issues).slice(0, 70)}` : ''
          }`,
        )
      })
      .catch(() => {
        metaW.setProperty(
          prop.TEXT,
          'Open phone Zepp → CALT Sync\nsettings — check full Base URL',
        )
      })

    messageBuilder
      .request({ method: 'GET_LOGS' })
      .then((res) => {
        const data = sidePayload(res)
        const logs = (data && data.logs) || []
        if (logs.length) {
          bodyW.setProperty(prop.TEXT, logs.slice(0, 5).map(formatEntry).join('\n\n'))
        }
      })
      .catch(() => {})

    createWidget(widget.BUTTON, {
      x: pad,
      y: yFrac(height, 0.82),
      w: contentW,
      h: Math.round(height * 0.1),
      text: 'Back',
      text_size: Math.round(width * 0.04),
      normal_color: 0x333333,
      press_color: 0x222222,
      click_func: () => back(),
    })
  },
})
