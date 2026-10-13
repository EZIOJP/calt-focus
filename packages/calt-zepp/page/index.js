/**
 * CALT Sync — one-tap Dump & Send.
 * Optional auto: watch alarm wakes this page every N hours (default 3) → Sync.
 */
import { createWidget, widget, prop } from '@zos/ui'
import {
  pauseDropWristScreenOff,
  pausePalmScreenOff,
  resetDropWristScreenOff,
  resetPageBrightTime,
  resetPalmScreenOff,
  setPageBrightTime,
} from '@zos/display'
import { push } from '@zos/router'
import { onGesture, offGesture, GESTURE_LEFT } from '@zos/interaction'
import { localStorage } from '@zos/storage'
import { log } from '@zos/utils'
import { buildHealthSnapshot, sleepDisplayMinutes } from './sensors'
import { screen } from './layout'
import { sidePayload } from '../shared/sidePayload'
import { markAutoRan } from './autoAlarm'
import {
  queueSnapshot,
  snapshotForDay,
  removeQueuedSnapshot,
  localDateKey,
  splitHealthChunks,
  loadChunkResume,
  saveChunkResume,
  clearChunkResume,
  pendingDays,
  markDaySynced,
} from './queue'

const logger = log.getLogger('calt-dump')
const AWAKE_MS = 10 * 60 * 1000
const CHUNK_GAP_MS = 1800

function holdAwake() {
  try {
    setPageBrightTime({ brightTime: AWAKE_MS })
  } catch (_) {}
  try {
    pauseDropWristScreenOff({ duration: AWAKE_MS })
  } catch (_) {}
  try {
    pausePalmScreenOff({ duration: AWAKE_MS })
  } catch (_) {}
}

function releaseAwake() {
  try {
    resetPageBrightTime()
  } catch (_) {}
  try {
    resetDropWristScreenOff()
  } catch (_) {}
  try {
    resetPalmScreenOff()
  } catch (_) {}
}

function after(ms) {
  return new Promise((resolve) => {
    if (typeof setTimeout === 'function') setTimeout(resolve, ms)
    else resolve()
  })
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
    return 'BLE timeout · keep watch + phone awake, tap Sync again'
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
  if (d.includes('413')) return 'Dump too large · retry Sync (resumes chunk)'
  if (d.includes('network') || d.includes('-2') || (d.includes('fail') && d.includes('fetch'))) {
    return 'Phone cannot reach PC · same Wi-Fi, hub :8765'
  }
  const short = raw.replace(/\s+/g, ' ').trim()
  return (short || 'Sync failed · swipe to log').slice(0, 96)
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
    releaseAwake()
    try {
      offGesture()
    } catch (_) {}
  },

  build() {
    const { messageBuilder } = getApp().globalData || getApp()._options.globalData || {}
    if (!messageBuilder) {
      const S0 = screen()
      createWidget(widget.FILL_RECT, {
        x: 0,
        y: 0,
        w: S0.width,
        h: S0.height,
        color: 0x000000,
      })
      const size = Math.round(Math.min(S0.width, S0.height) * 0.62)
      createWidget(widget.BUTTON, {
        x: Math.round((S0.width - size) / 2),
        y: Math.round((S0.height - size) / 2),
        w: size,
        h: size,
        radius: Math.round(size / 2),
        text: 'Wait',
        color: 0xffffff,
        text_size: Math.round(S0.width * 0.08),
        normal_color: 0x111111,
        press_color: 0x2a2a2a,
        click_func: () => {},
      })
      return
    }

    const S = screen()
    const { width, height } = S
    const syncSize = Math.round(Math.min(width, height) * 0.62)
    const syncX = Math.round((width - syncSize) / 2)
    const syncY = Math.round((height - syncSize) / 2)

    createWidget(widget.FILL_RECT, {
      x: 0,
      y: 0,
      w: width,
      h: height,
      color: 0x000000,
    })

    this.syncBtn = createWidget(widget.BUTTON, {
      x: syncX,
      y: syncY,
      w: syncSize,
      h: syncSize,
      radius: Math.round(syncSize / 2),
      text: 'Sync',
      color: 0xffffff,
      text_size: Math.round(width * 0.08),
      normal_color: 0x111111,
      press_color: 0x2a2a2a,
      click_func: () => {
        this.doSend(messageBuilder)
      },
    })

    try {
      const g = getApp().globalData || (getApp()._options && getApp()._options.globalData) || {}
      if (g.autoLaunch) {
        g.autoLaunch = false
        setTimeout(() => {
          try {
            this.doSend(messageBuilder, { fromAuto: true })
          } catch (e) {
            logger.log(`auto ${e}`)
            this.setError('Retry')
          }
        }, 400)
      }
    } catch (_) {}

    try {
      if (localStorage.getItem('calt_last_error')) this.setButton('Retry')
    } catch (_) {}
  },

  setButton(text) {
    if (!this.syncBtn) return
    try {
      this.syncBtn.setProperty(prop.TEXT, text || 'Sync')
    } catch (_) {}
  },

  setProgress(text) {
    persistProgress(text)
    this.setButton(text || 'Sync')
  },

  setStatus() {},

  setError(text) {
    persistError(text)
    this.setButton('Retry')
    releaseAwake()
  },

  clearError() {
    persistError('')
  },

  doSend(messageBuilder, opts) {
    if (this._syncing) return
    const fromAuto = !!(opts && opts.fromAuto)
    this._syncing = true
    this.clearError()
    holdAwake()
    this.setButton('…')

    const today = localDateKey()
    const health = buildHealthSnapshot('full')
    this._preview = health
    queueSnapshot(health, { force: true })

    const days = pendingDays()
    const resume = loadChunkResume()
    if (resume && resume.day && days.indexOf(resume.day) < 0) days.unshift(resume.day)
    if (!days.length) {
      this._syncing = false
      this.setButton('Sync')
      releaseAwake()
      return
    }

    const self = this
    let resumeApplied = false
    const preview = this._preview

    const finish = () => {
      clearChunkResume()
      self._syncing = false
      if (fromAuto) markAutoRan()
      persistError('')
      self.setButton('OK')
      releaseAwake()
      after(1200).then(() => {
        if (!self._syncing) self.setButton('Sync')
      })
    }

    const runDay = (dayIndex) => {
      if (dayIndex >= days.length) {
        finish()
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
          markDaySynced(day)
          removeQueuedSnapshot(day)
          saveChunkResume(days[dayIndex + 1] || '', 0)
          after(CHUNK_GAP_MS).then(() => runDay(dayIndex + 1))
          return
        }
        holdAwake()
        const chunk = chunks[partIndex]
        const overall = dayIndex * chunks.length + partIndex + 1
        const overallTotal = Math.max(1, days.length * chunks.length)
        self.setButton(`${overall}/${overallTotal}`)
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
            if (!result.healthOk) {
              saveChunkResume(day, partIndex)
              self._syncing = false
              logger.log(fmtSyncError(null, result))
              self.setError('Retry')
              return
            }
            saveChunkResume(day, partIndex + 1)
            after(CHUNK_GAP_MS).then(() => runPart(partIndex + 1))
          })
          .catch((e) => {
            saveChunkResume(day, partIndex)
            self._syncing = false
            logger.log(`send ${e}`)
            self.setError('Retry')
          })
      }

      runPart(startPart)
    }

    after(600).then(() => runDay(0))
  },
})
