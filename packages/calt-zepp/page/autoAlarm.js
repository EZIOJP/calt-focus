/**
 * Persist a repeating Dump & Send wake every N hours (default 3).
 * Uses @zos/alarm → page/index with param auto=1.
 */
import { localStorage } from '@zos/storage'
import { set, cancel, getAllAlarms, REPEAT_HOUR } from '@zos/alarm'

const KEY_ON = 'calt_auto_sync'
const KEY_HOURS = 'calt_auto_hours'
const KEY_ALARM = 'calt_auto_alarm_id'
const KEY_LAST = 'calt_auto_last_at'
const DEFAULT_HOURS = 3

export function autoSyncEnabled() {
  try {
    return localStorage.getItem(KEY_ON) === '1'
  } catch (_) {
    return false
  }
}

export function autoSyncHours() {
  try {
    const n = Number(localStorage.getItem(KEY_HOURS) || DEFAULT_HOURS)
    if (!Number.isFinite(n)) return DEFAULT_HOURS
    return Math.max(1, Math.min(12, Math.round(n)))
  } catch (_) {
    return DEFAULT_HOURS
  }
}

export function setAutoSyncEnabled(on) {
  try {
    localStorage.setItem(KEY_ON, on ? '1' : '0')
  } catch (_) {}
}

export function setAutoSyncHours(hours) {
  const n = Number(hours)
  const v = Number.isFinite(n) ? Math.max(1, Math.min(12, Math.round(n))) : DEFAULT_HOURS
  try {
    localStorage.setItem(KEY_HOURS, String(v))
  } catch (_) {}
}

export function markAutoRan() {
  try {
    localStorage.setItem(KEY_LAST, new Date().toISOString())
  } catch (_) {}
}

export function lastAutoAt() {
  try {
    return localStorage.getItem(KEY_LAST) || ''
  } catch (_) {
    return ''
  }
}

function clearStoredAlarms() {
  try {
    const ids = getAllAlarms() || []
    for (let i = 0; i < ids.length; i++) {
      try {
        cancel(ids[i])
      } catch (_) {}
    }
  } catch (_) {
    try {
      const id = Number(localStorage.getItem(KEY_ALARM) || 0)
      if (id) cancel(id)
    } catch (__) {}
  }
  try {
    localStorage.removeItem(KEY_ALARM)
  } catch (_) {}
}

/**
 * Schedule (or clear) the repeating auto Dump & Send alarm.
 * @returns {{ ok: boolean, id?: number, hours?: number, detail?: string }}
 */
export function applyAutoAlarm() {
  const on = autoSyncEnabled()
  const hours = autoSyncHours()
  try {
    clearStoredAlarms()
    if (!on) {
      return { ok: true, hours, detail: 'auto off' }
    }
    const delaySec = hours * 3600
    const id = set({
      url: 'page/index',
      delay: delaySec,
      param: 'auto=1',
      store: true,
      repeat_type: REPEAT_HOUR,
      repeat_period: hours,
      repeat_duration: 1,
    })
    if (!id) {
      return { ok: false, hours, detail: 'alarm set failed (need device:os.alarm)' }
    }
    try {
      localStorage.setItem(KEY_ALARM, String(id))
    } catch (_) {}
    return { ok: true, id, hours, detail: `every ${hours}h` }
  } catch (e) {
    return {
      ok: false,
      hours,
      detail: String((e && e.message) || e || 'alarm error'),
    }
  }
}

export function autoStatusLine() {
  if (!autoSyncEnabled()) return 'Auto sync OFF'
  const hours = autoSyncHours()
  const last = lastAutoAt()
  const when = last ? last.replace('T', ' ').slice(0, 16) : 'never'
  return `Auto every ${hours}h · last ${when}`
}

/** True when this launch was triggered by the auto alarm. */
export function isAutoLaunchParam(param) {
  const s = String(param == null ? '' : param)
  return s.indexOf('auto=1') >= 0 || s === 'auto'
}
