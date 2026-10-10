/**
 * Health dumper — Dump captures, Send flushes queue.
 * Optional auto: watch alarm wakes this page every N hours (default 3) → Dump & Send.
 */
import { createWidget, widget, align, text_style, prop } from '@zos/ui'
import { push } from '@zos/router'
import { onGesture, offGesture, GESTURE_LEFT } from '@zos/interaction'
import { localStorage } from '@zos/storage'
import { log } from '@zos/utils'
import { buildHealthSnapshot, snapshotSummary, sleepDisplayMinutes } from './sensors'
import { screen } from './layout'
import { sidePayload } from '../shared/sidePayload'
import { autoStatusLine, markAutoRan } from './autoAlarm'
import {
  queueSnapshot,
  snapshotForDay,
  removeQueuedSnapshot,
  localDateKey,
  splitHealthChunks,
  loadChunkResume,
  saveChunkResume,
  clearChunkResume,
  queuedDays,
  queueDepth,
  pendingDays,
  markDaySynced,
  lastSyncedDay,
  gapDays,
} from './queue'

const logger = log.getLogger('calt-dump')
const COLOR_OK = 0x88c0bb
const COLOR_BUSY = 0xf0c674
const COLOR_ERR = 0xff6666
const COLOR_MUTED = 0xaaaaaa

function bar(done, total) {
  const n = Math.max(1, Number(total) || 1)
  const d = Math.max(0, Math.min(n, Number(done) || 0))
  let s = ''
  for (let i = 0; i < n; i++) s += i < d ? '#' : '-'
  return `[${s}] ${d}/${n}`
}

function fmtSyncError(err, result) {
  const pieces = []
  if (result && result.errors && result.errors.length) {
    for (let i = 0; i < result.errors.length; i++) pieces.push(String(result.errors[i]))
  }
  if (err) pieces.push(String(err && err.message ? err.message : err))
  if (result && result.summary) pieces.push(String(result.summary))
  if (result && result.diag) pieces.push(String(result.diag))
  const raw = pieces.join(' · ')
  const d = raw.toLowerCase()
  if (d.includes('timed out') || d.includes('timeout')) {
    return 'BLE timeout · keep watch + phone awake, tap Send again'
  }
  if (d.includes('localhost') || d.includes('127.0.0.1')) {
    return 'Use PC LAN IP in phone settings, not localhost'
  }
  if (d.includes('base url') || d.includes('no base')) {
    return 'Set Base URL in phone Zepp → CALT Sync'
  }
  if (d.includes('401') || d.includes('unauthorized')) {
    return 'Token rejected · calt-local-wearables'
  }
  if (d.includes('413')) return 'Dump too large · retry Send (resumes chunk)'
  if (d.includes('network') || d.includes('-2') || (d.includes('fail') && d.includes('fetch'))) {
    return 'Phone cannot reach PC · same Wi-Fi, hub :8765 or API :8000'
  }
  const short = raw.replace(/\s+/g, ' ').trim()
  return (short || 'Send failed · swipe to log').slice(0, 96)
}

function pcReceivedLine() {
  let at = ''
  try {
    at = localStorage.getItem('calt_last_received_at') || ''
  } catch (_) {}
  if (!at) return 'PC received: never'
  return `PC received ${String(at).replace('T', ' ').slice(0, 19)}`
}

function rememberPcReceived(iso) {
  if (!iso) return
  try {
    localStorage.setItem('calt_last_received_at', String(iso))
  } catch (_) {}
}

function persistProgress(text) {
  try {
    localStorage.setItem('calt_last_progress', text || '')
  } catch (_) {}
}

function persistError(text) {
  try {
    if (text) localStorage.setItem('calt_last_error', text)
    else localStorage.removeItem('calt_last_error')
  } catch (_) {}
}

function saveWatchLog(result, health) {
  let logs = []
  try {
    logs = JSON.parse(localStorage.getItem('calt_sync_log') || '[]')
  } catch (_) {
    logs = []
  }
  if (!Array.isArray(logs)) logs = []
  logs.unshift({
    at: result.plansFetchedAt || new Date().toISOString(),
    ok: !!(result.healthOk && !(result.errors || []).length),
    summary: result.summary || '',
    errors: result.errors || [],
    diag: result.diag || '',
    host: result.host || '',
    steps: (health.activity && health.activity.steps) || result.steps,
    sleep_min: sleepDisplayMinutes(health) || result.sleepMin,
    wrote_life: !!result.wroteLife,
    base: result.base || '',
    replay: !!(result.replayed || result.duplicate),
  })
  logs = logs.slice(0, 20)
  try {
    localStorage.setItem('calt_sync_log', JSON.stringify(logs))
  } catch (_) {}
}

function cacheSyncResult(result) {
  try {
    localStorage.setItem('calt_last_summary', result.summary || result.diag || '')
    const snap = {
      steps: result.steps,
      sleep_min: result.sleepMin,
      hr: result.hr,
      stand: result.standHours,
      battery: result.batteryPct,
    }
    localStorage.setItem('calt_last_payload', JSON.stringify(snap))
  } catch (_) {}
}

Page({
  onInit() {
    onGesture((event) => {
      if (event === GESTURE_LEFT) {
        push({ url: 'page/settings' })
        return true
      }
      return false
    })
  },

  onDestroy() {
    try {
      offGesture()
    } catch (_) {}
  },

  build() {
    const { messageBuilder } = getApp().globalData || getApp()._options.globalData || {}
    if (!messageBuilder) {
      createWidget(widget.TEXT, {
        x: 20,
        y: 80,
        w: 440,
        h: 80,
        color: 0xff6666,
        text_size: 22,
        align_h: align.CENTER_H,
        text: 'BLE not ready — reopen app',
      })
      return
    }

    const S = screen()
    const { pad, contentW, width, height } = S
    const btnH = Math.round(height * 0.14)
    const gap = Math.round(pad * 0.35)

    // Do not hit every sensor just to paint the home screen.
    const queuedToday = snapshotForDay(localDateKey())
    this._preview = queuedToday || null

    createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.06),
      w: contentW,
      h: Math.round(height * 0.07),
      color: 0xffffff,
      text_size: Math.round(width * 0.055),
      align_h: align.CENTER_H,
      text: 'CALT Dump',
    })

    this.snapW = createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.14),
      w: contentW,
      h: Math.round(height * 0.16),
      color: 0x88c0bb,
      text_size: Math.round(width * 0.036),
      align_h: align.CENTER_H,
      text_style: text_style.WRAP,
      text: queuedToday ? snapshotSummary(queuedToday) : 'Tap Dump today',
    })

    this.queueW = createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.30),
      w: contentW,
      h: Math.round(height * 0.06),
      color: 0xf0c674,
      text_size: Math.round(width * 0.03),
      align_h: align.CENTER_H,
      text_style: text_style.WRAP,
      text: `Queue ${queueDepth()}/7 days`,
    })

    this.progressW = createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.36),
      w: contentW,
      h: Math.round(height * 0.06),
      color: COLOR_MUTED,
      text_size: Math.round(width * 0.03),
      align_h: align.CENTER_H,
      text_style: text_style.WRAP,
      text: 'Idle',
    })

    this.statusW = createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.42),
      w: contentW,
      h: Math.round(height * 0.08),
      color: COLOR_MUTED,
      text_size: Math.round(width * 0.028),
      align_h: align.CENTER_H,
      text_style: text_style.WRAP,
      text: pcReceivedLine(),
    })

    this.autoW = createWidget(widget.TEXT, {
      x: pad,
      y: Math.round(height * 0.485),
      w: contentW,
      h: Math.round(height * 0.035),
      color: 0x88c0bb,
      text_size: Math.round(width * 0.026),
      align_h: align.CENTER_H,
      text_style: text_style.WRAP,
      text: autoStatusLine(),
    })

    let y = Math.round(height * 0.52)
    createWidget(widget.BUTTON, {
      x: pad,
      y,
      w: contentW,
      h: btnH,
      radius: Math.round(btnH * 0.22),
      text: 'Dump today',
      text_size: Math.round(width * 0.045),
      normal_color: 0x2a4a6a,
      press_color: 0x1e364d,
      click_func: () => this.doDump(),
    })
    y += btnH + gap

    createWidget(widget.BUTTON, {
      x: pad,
      y,
      w: contentW,
      h: btnH,
      radius: Math.round(btnH * 0.22),
      text: 'Dump & Send',
      text_size: Math.round(width * 0.045),
      normal_color: 0x3d5a80,
      press_color: 0x2c4160,
      click_func: () => {
        this.doDump()
        this.doSend(messageBuilder)
      },
    })
    y += btnH + gap

    createWidget(widget.BUTTON, {
      x: pad,
      y,
      w: contentW,
      h: btnH,
      radius: Math.round(btnH * 0.22),
      text: 'Send queue',
      text_size: Math.round(width * 0.045),
      normal_color: 0x1a9b8e,
      press_color: 0x147a70,
      click_func: () => this.doSend(messageBuilder),
    })
    y += btnH + gap

    // Auto-launch from alarm: dump + send once UI + BLE are ready.
    try {
      const g = getApp().globalData || (getApp()._options && getApp()._options.globalData) || {}
      if (g.autoLaunch) {
        g.autoLaunch = false
        this.setStatus('Auto sync…', COLOR_BUSY)
        setTimeout(() => {
          try {
            this.doDump()
            this.doSend(messageBuilder, { fromAuto: true })
          } catch (e) {
            logger.log(`auto ${e}`)
            this.setError('Auto sync failed · tap Dump & Send')
          }
        }, 400)
      }
    } catch (_) {}

    createWidget(widget.TEXT, {
      x: pad,
      y,
      w: contentW,
      h: Math.round(height * 0.06),
      color: 0x666666,
      text_size: Math.round(width * 0.028),
      align_h: align.CENTER_H,
      text: 'Swipe · settings / log',
    })

    try {
      const last = localStorage.getItem('calt_last_summary')
      const err = localStorage.getItem('calt_last_error')
      const prog = localStorage.getItem('calt_last_progress')
      if (prog) this.setProgress(prog, COLOR_MUTED)
      if (err) this.setError(err)
      else if (last) this.setStatus(last, COLOR_MUTED)
    } catch (_) {}
  },

  setProgress(text, color) {
    persistProgress(text)
    if (!this.progressW) return
    this.progressW.setProperty(prop.TEXT, text || 'Idle')
    try {
      this.progressW.setProperty(prop.COLOR, color == null ? COLOR_BUSY : color)
    } catch (_) {}
  },

  setStatus(text, color) {
    if (this.statusW) {
      this.statusW.setProperty(prop.TEXT, text)
      try {
        this.statusW.setProperty(prop.COLOR, color == null ? COLOR_MUTED : color)
      } catch (_) {}
    }
  },

  setError(text) {
    persistError(text)
    this.setProgress('Failed · tap Send to retry', COLOR_ERR)
    this.setStatus(text, COLOR_ERR)
  },

  clearError() {
    persistError('')
  },

  refreshQueueLabel() {
    if (!this.queueW) return
    const pending = pendingDays()
    const mark = lastSyncedDay()
    const tail = mark ? `thru ${mark}` : 'never synced'
    this.queueW.setProperty(
      prop.TEXT,
      pending.length ? `Pending ${pending.length} · ${tail}` : `Up to date · ${tail}`,
    )
  },

  doDump() {
    this.clearError()
    this.setProgress('Capturing sensors…', COLOR_BUSY)
    this.setStatus('Please wait', COLOR_BUSY)
    const health = buildHealthSnapshot('full')
    this._preview = health
    queueSnapshot(health, { force: true })
    if (this.snapW) this.snapW.setProperty(prop.TEXT, snapshotSummary(health))
    this.refreshQueueLabel()
    this.setProgress('Queued today', COLOR_OK)
    this.setStatus('Press Send queue', COLOR_OK)
  },

  doSend(messageBuilder, opts) {
    if (this._syncing) {
      this.setStatus('Already sending…', COLOR_BUSY)
      return
    }
    const fromAuto = !!(opts && opts.fromAuto)
    this._syncing = true
    this.clearError()
    this.setProgress('Starting…', COLOR_BUSY)
    this.setStatus(fromAuto ? 'Auto sending…' : 'Sending to phone', COLOR_BUSY)

    const today = localDateKey()
    // Ensure today exists if user forgot Dump
    if (queuedDays().indexOf(today) < 0) {
      const health = buildHealthSnapshot('full')
      this._preview = health
      queueSnapshot(health, { force: true })
      if (this.snapW) this.snapW.setProperty(prop.TEXT, snapshotSummary(health))
    }

    // Oldest first, so a run always fills forward from the watermark to today.
    const days = pendingDays()
    const resume = loadChunkResume()
    if (resume && resume.day && days.indexOf(resume.day) < 0) {
      days.unshift(resume.day)
    }
    if (!days.length) {
      this._syncing = false
      this.setProgress('Idle', COLOR_MUTED)
      this.setStatus('Nothing to send · Dump today first', COLOR_ERR)
      return
    }

    const self = this
    let resumeApplied = false
    const preview = this._preview || buildHealthSnapshot('full')

    const runDay = (dayIndex) => {
      if (dayIndex >= days.length) {
        clearChunkResume()
        self._syncing = false
        self.refreshQueueLabel()
        self.setProgress(bar(days.length * 4, days.length * 4), COLOR_OK)
        // Gaps are days the watch never captured. Sensors only report the
        // current day, so no later run can recover them — say so instead of
        // reporting a clean fill.
        const gaps = gapDays(today)
        if (fromAuto) markAutoRan()
        if (self.autoW) {
          try {
            self.autoW.setProperty(prop.TEXT, autoStatusLine())
          } catch (_) {}
        }
        const stamp = self._pcReceivedAt ? pcReceivedLine() : 'PC received: never'
        self.setStatus(
          gaps.length
            ? `${fromAuto ? 'Auto · ' : ''}Sent ${days.length}d · ${gaps.length}d never captured · ${stamp}`
            : `${fromAuto ? 'Auto · ' : ''}Done · ${stamp}`,
          gaps.length ? COLOR_BUSY : COLOR_OK,
        )
        persistError('')
        return
      }
      const day = days[dayIndex]
      const dayHealth = snapshotForDay(day) || (day === today ? preview : null)
      if (!dayHealth) {
        removeQueuedSnapshot(day)
        runDay(dayIndex + 1)
        return
      }
      const chunks = splitHealthChunks(dayHealth)
      let startPart = 0
      if (!resumeApplied && resume && resume.day === day) {
        startPart = resume.nextPart || 0
        resumeApplied = true
      }

      const runPart = (partIndex) => {
        if (partIndex >= chunks.length) {
          // Watermark advances only after every chunk of the day was ACKed.
          markDaySynced(day)
          removeQueuedSnapshot(day)
          saveChunkResume(days[dayIndex + 1] || '', 0)
          self.refreshQueueLabel()
          runDay(dayIndex + 1)
          return
        }
        const chunk = chunks[partIndex]
        const overall = dayIndex * chunks.length + partIndex + 1
        const overallTotal = days.length * chunks.length
        self.setProgress(
          `Day ${dayIndex + 1}/${days.length} · ${chunk.label}\n${bar(partIndex + 1, chunks.length)}  all ${overall}/${overallTotal}`,
          COLOR_BUSY,
        )
        self.setStatus(`${day} sending…`, COLOR_BUSY)
        messageBuilder
          .request({
            method: 'SYNC',
            params: {
              health: chunk.health,
              localDate: dayHealth.local_date || day,
              tz_offset_min: dayHealth.tz_offset_min,
              queuedSleepSnapshot: day !== today,
              skipFollowup: true,
              chunk: {
                part: partIndex + 1,
                total: chunks.length,
                label: chunk.label,
                dump: 'processed_v1',
                day,
                chunk_id: chunk.chunk_id,
                dump_id: dayHealth.dump_id,
                checksum: chunk.checksum,
              },
            },
          })
          .then((res) => {
            const result = sidePayload(res)
            cacheSyncResult(result)
            saveWatchLog(result, dayHealth)
            const stamped =
              (result.serverEcho && result.serverEcho.last_received_at) || result.receivedAt || ''
            if (stamped) {
              rememberPcReceived(stamped)
              self._pcReceivedAt = stamped
            }
            if (!result.healthOk) {
              saveChunkResume(day, partIndex)
              self._syncing = false
              self.setError(
                `${day} ${chunk.label} ${partIndex + 1}/${chunks.length} · ${fmtSyncError(null, result)}`,
              )
              return
            }
            saveChunkResume(day, partIndex + 1)
            self.setProgress(
              `Day ${dayIndex + 1}/${days.length} · ${chunk.label} OK\n${bar(partIndex + 1, chunks.length)}`,
              COLOR_OK,
            )
            runPart(partIndex + 1)
          })
          .catch((e) => {
            saveChunkResume(day, partIndex)
            self._syncing = false
            self.setError(
              `${day} ${chunk.label} ${partIndex + 1}/${chunks.length} · ${fmtSyncError(e, null)}`,
            )
            logger.log(`send ${e}`)
          })
      }

      runPart(startPart)
    }

    runDay(0)
  },
})
