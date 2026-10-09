/**
 * Local 7-day full-dump queue + chunk resume + dump identity.
 */
import { localStorage } from '@zos/storage'

export const PENDING_QUEUE_KEY = 'calt_pending_sync_queue'
export const PENDING_QUEUE_DAYS = 7
export const CHUNK_RESUME_KEY = 'calt_chunk_resume'
export const SYNCED_DAYS_KEY = 'calt_synced_days'
export const LAST_SYNCED_DAY_KEY = 'calt_last_synced_day'

/**
 * How long a day that the server has NOT accepted stays on the watch.
 * Accepted days are dropped after PENDING_QUEUE_DAYS since the server owns
 * them; unsent days get much longer so a few weeks away from the PC cannot
 * silently delete health data that was never delivered.
 */
export const UNSENT_KEEP_DAYS = 30
const SYNCED_HISTORY_MAX = 60
const DAY_MS = 24 * 60 * 60 * 1000

export function localDateKey() {
  const d = new Date()
  const m = `${d.getMonth() + 1}`.padStart(2, '0')
  const day = `${d.getDate()}`.padStart(2, '0')
  return `${d.getFullYear()}-${m}-${day}`
}

function readQueue() {
  try {
    const queue = JSON.parse(localStorage.getItem(PENDING_QUEUE_KEY) || '{}') || {}
    if (!queue || typeof queue !== 'object' || Array.isArray(queue)) return {}
    return queue
  } catch (_) {
    return {}
  }
}

function writeQueue(queue) {
  try {
    localStorage.setItem(PENDING_QUEUE_KEY, JSON.stringify(queue))
  } catch (_) {}
}

function readSynced() {
  try {
    const raw = JSON.parse(localStorage.getItem(SYNCED_DAYS_KEY) || '{}') || {}
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return {}
    return raw
  } catch (_) {
    return {}
  }
}

function writeSynced(map) {
  try {
    localStorage.setItem(SYNCED_DAYS_KEY, JSON.stringify(map))
  } catch (_) {}
}

function dayKeyFrom(ms) {
  const d = new Date(ms)
  const m = `${d.getMonth() + 1}`.padStart(2, '0')
  const day = `${d.getDate()}`.padStart(2, '0')
  return `${d.getFullYear()}-${m}-${day}`
}

function pruneQueue(queue) {
  const synced = readSynced()
  const now = Date.now()
  Object.keys(queue).forEach((key) => {
    const stamp = Date.parse(`${key}T00:00:00`)
    if (!Number.isFinite(stamp)) {
      delete queue[key]
      return
    }
    const ageDays = (now - stamp) / DAY_MS
    const keep = synced[key] ? PENDING_QUEUE_DAYS : UNSENT_KEEP_DAYS
    if (ageDays > keep) delete queue[key]
  })
  return queue
}

/** Record that every chunk of `day` was accepted, and advance the watermark. */
export function markDaySynced(day) {
  if (!day) return
  const map = readSynced()
  map[day] = new Date().toISOString()
  const keys = Object.keys(map).sort()
  while (keys.length > SYNCED_HISTORY_MAX) {
    delete map[keys.shift()]
  }
  writeSynced(map)
  const prev = lastSyncedDay()
  if (!prev || day > prev) {
    try {
      localStorage.setItem(LAST_SYNCED_DAY_KEY, day)
    } catch (_) {}
  }
}

export function lastSyncedDay() {
  try {
    const v = String(localStorage.getItem(LAST_SYNCED_DAY_KEY) || '')
    return /^\d{4}-\d{2}-\d{2}$/.test(v) ? v : ''
  } catch (_) {
    return ''
  }
}

/**
 * Days still awaiting delivery, oldest first.
 *
 * The queue itself is the source of truth: a day is removed only once every
 * chunk is ACKed, and a re-dump puts it back. Deliberately NOT filtered by the
 * synced map, or re-dumping a day already sent today would never resend.
 */
export function pendingDays() {
  return queuedDays()
}

/**
 * Days between the watermark and today that hold no snapshot and were never
 * synced. The watch sensors only report the current day, so these can never be
 * reconstructed — they are surfaced instead of being silently skipped.
 */
export function gapDays(today) {
  const from = lastSyncedDay()
  const end = Date.parse(`${today}T00:00:00`)
  let cur = Date.parse(`${from}T00:00:00`)
  if (!from || !Number.isFinite(cur) || !Number.isFinite(end)) return []

  const queue = readQueue()
  const synced = readSynced()
  const out = []
  for (cur += DAY_MS; cur < end; cur += DAY_MS) {
    const key = dayKeyFrom(cur)
    if (!queue[key] && !synced[key]) out.push(key)
  }
  return out
}

function hasSleep(health) {
  return !!(health && health.sleep && health.sleep.total_min)
}

/** Simple stable checksum for replay identity (not crypto). */
export function payloadChecksum(obj) {
  const s = JSON.stringify(obj || {})
  let h = 2166136261
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i)
    h = Math.imul(h, 16777619)
  }
  return `h${(h >>> 0).toString(16)}`
}

export function makeDumpId(day, capturedAt) {
  return `dump_${day}_${String(capturedAt || Date.now()).replace(/[^0-9A-Za-z]/g, '').slice(0, 20)}`
}

/**
 * Queue today's snapshot. force=true overwrites (manual Dump).
 */
export function queueSnapshot(health, opts) {
  if (!health) return
  const force = !!(opts && opts.force)
  try {
    const day = (opts && opts.day) || localDateKey()
    const queue = pruneQueue(readQueue())
    const current = queue[day]
    const incomingFull = health.dump === 'processed_v1'
    const incomingSleep = hasSleep(health)
    const currentSleep = hasSleep(current)

    const stamped = Object.assign({}, health)
    if (!stamped.captured_at) stamped.captured_at = new Date().toISOString()
    if (!stamped.local_date) stamped.local_date = day
    if (stamped.tz_offset_min == null) {
      try {
        stamped.tz_offset_min = -new Date().getTimezoneOffset()
      } catch (_) {}
    }
    if (!stamped.dump_id) stamped.dump_id = makeDumpId(day, stamped.captured_at)
    stamped.checksum = payloadChecksum({
      sleep: stamped.sleep,
      activity: stamped.activity,
      heart: stamped.heart,
      stress: stamped.stress,
      spo2: stamped.spo2,
      workouts: stamped.workouts,
      pai: stamped.pai,
      calorie: stamped.calorie,
    })

    if (force || !current) {
      if (force || incomingSleep || incomingFull) {
        if (force && !incomingSleep && currentSleep) {
          stamped.sleep = current.sleep
        }
        queue[day] = stamped
      }
    } else if (incomingFull && current.dump !== 'processed_v1') {
      if (!incomingSleep && currentSleep) stamped.sleep = current.sleep
      queue[day] = stamped
    } else if (incomingSleep && !currentSleep) {
      queue[day] = stamped
    }
    writeQueue(queue)
  } catch (_) {}
}

export function queuedDays() {
  const queue = pruneQueue(readQueue())
  writeQueue(queue)
  return Object.keys(queue).sort()
}

export function queueDepth() {
  return queuedDays().length
}

export function oldestQueuedSnapshot() {
  try {
    const queue = pruneQueue(readQueue())
    const day = Object.keys(queue).sort()[0]
    return day ? { day, health: queue[day] } : null
  } catch (_) {
    return null
  }
}

export function snapshotForDay(day) {
  const queue = readQueue()
  return queue[day] || null
}

export function removeQueuedSnapshot(day) {
  try {
    const queue = readQueue()
    delete queue[day]
    writeQueue(queue)
  } catch (_) {}
}

export function loadChunkResume() {
  try {
    const raw = JSON.parse(localStorage.getItem(CHUNK_RESUME_KEY) || 'null')
    if (!raw || !raw.day) return null
    return { day: String(raw.day), nextPart: Number(raw.nextPart) || 0 }
  } catch (_) {
    return null
  }
}

export function saveChunkResume(day, nextPart) {
  try {
    localStorage.setItem(CHUNK_RESUME_KEY, JSON.stringify({ day, nextPart }))
  } catch (_) {}
}

export function clearChunkResume() {
  try {
    localStorage.removeItem(CHUNK_RESUME_KEY)
  } catch (_) {}
}

/** Split a full snapshot into small ingest parts with stable chunk ids. */
export function splitHealthChunks(health) {
  const h = health || {}
  const dump = h.dump || 'processed_v2'
  const dumpId = h.dump_id || makeDumpId(h.local_date || localDateKey(), h.captured_at)
  const clock = {
    captured_at: h.captured_at,
    local_date: h.local_date,
    tz_offset_min: h.tz_offset_min,
  }
  const heartScalars = h.heart
    ? {
        last: h.heart.last,
        resting: h.heart.resting,
        max: h.heart.max,
        avg: h.heart.avg,
        daily_summary: h.heart.daily_summary,
        hr_zones: h.heart.hr_zones || h.hr_zones,
      }
    : undefined
  const heartSeries =
    h.heart && h.heart.today_min && h.heart.today_min.length
      ? {
          today_min: h.heart.today_min,
          today_count: h.heart.today_count,
        }
      : undefined

  const parts = [
    { label: 'Sleep', health: { dump, dump_id: dumpId, ...clock, sleep: h.sleep, capabilities: h.capabilities } },
    {
      label: 'Activity',
      health: {
        dump,
        dump_id: dumpId,
        ...clock,
        activity: h.activity,
        calorie: h.calorie,
        distance: h.distance,
        stand: h.stand,
        battery: h.battery,
        sitting: h.sitting,
        fat_burn: h.fat_burn,
        capabilities: h.capabilities,
      },
    },
    { label: 'Heart', health: { dump, dump_id: dumpId, ...clock, heart: heartScalars } },
    {
      label: 'Series',
      health: {
        dump,
        dump_id: dumpId,
        ...clock,
        heart: heartSeries ? { series: true, ...heartSeries } : undefined,
        spo2: h.spo2
          ? { recent: h.spo2.recent, last_day: h.spo2.last_day, last_day_avg: h.spo2.last_day_avg }
          : undefined,
        stress: h.stress
          ? { today_by_hour: h.stress.today_by_hour, last_week: h.stress.last_week }
          : undefined,
      },
    },
    {
      label: 'Extras',
      health: {
        dump,
        dump_id: dumpId,
        ...clock,
        stress: h.stress ? { value: h.stress.value, time: h.stress.time } : undefined,
        spo2: h.spo2 ? { value: h.spo2.value, time: h.spo2.time, retCode: h.spo2.retCode } : undefined,
        pai: h.pai,
        temperature: h.temperature,
        weather: h.weather,
        workouts: h.workouts,
        hr_zones: h.hr_zones,
        meta_device: h.meta_device,
        capabilities: h.capabilities,
      },
    },
  ]
  return parts.map((p, i) => ({
    ...p,
    chunk_id: `${dumpId}_${i + 1}`,
    checksum: payloadChecksum(p.health),
  }))
}
