/**
 * Health snapshots for manual dump.
 * Full = processed dump with downsampled series (no continuous PPG).
 * Missing sensors stay absent — never fabricate zeroes.
 */
import {
  Sleep,
  HeartRate,
  Step,
  Calorie,
  Distance,
  BloodOxygen,
  Stress,
  Pai,
  Stand,
  Battery,
  FatBurning,
  Weather,
  Time,
} from '@zos/sensor'
import { getDeviceInfo } from '@zos/device'
import { getProfile } from '@zos/user'
import { formatHoursMins } from '../shared/timeFmt'

function safe(fn, fallback) {
  try {
    return fn()
  } catch (_) {
    return fallback
  }
}

function asInt(v, fallback) {
  if (v === undefined || v === null || v === '') return fallback
  const n = Number(v)
  return Number.isFinite(n) ? Math.round(n) : fallback
}

/** Watch wall clock — never use the phone's Date for the calendar day. */
export function watchClock() {
  const d = new Date()
  const y = d.getFullYear()
  const mo = `${d.getMonth() + 1}`.padStart(2, '0')
  const day = `${d.getDate()}`.padStart(2, '0')
  let tz = 0
  try {
    tz = -d.getTimezoneOffset()
  } catch (_) {
    try {
      tz = new Time().getTimezoneOffset ? -new Time().getTimezoneOffset() : 0
    } catch (__) {
      tz = 0
    }
  }
  return {
    local_date: `${y}-${mo}-${day}`,
    tz_offset_min: tz,
    captured_at: d.toISOString(),
    os: 6,
  }
}

/** Keep every Nth sample so POST stays small (HR today can be 1440). */
function downsample(arr, maxPoints) {
  if (!arr || !arr.length) return []
  const max = maxPoints || 288
  if (arr.length <= max) return arr.slice()
  const step = Math.ceil(arr.length / max)
  const out = []
  for (let i = 0; i < arr.length; i += step) out.push(arr[i])
  if (out[out.length - 1] !== arr[arr.length - 1]) out.push(arr[arr.length - 1])
  return out
}

function normalizeNaps(raw) {
  if (!raw || !raw.length) return []
  const out = []
  for (let i = 0; i < raw.length; i++) {
    const n = raw[i] || {}
    const start = asInt(n.start != null ? n.start : n.startTime, null)
    const stop = asInt(n.stop != null ? n.stop : n.endTime != null ? n.endTime : n.end, null)
    let length = asInt(n.length, null)
    if (length == null && start != null && stop != null && stop > start) length = stop - start
    if (start == null || (length == null && stop == null)) continue
    if (length != null && length < 5) continue
    out.push({
      start,
      stop: stop != null ? stop : start + (length || 0),
      length: length != null ? length : stop - start,
    })
  }
  return out
}

function collectSleep(full) {
  const sleepSensor = new Sleep()
  safe(() => sleepSensor.updateInfo(), null)
  safe(() => sleepSensor.updateInfo(), null)
  const info = safe(() => sleepSensor.getInfo(), {}) || {}

  let stages = []
  if (full) {
    try {
      const consts = sleepSensor.getStageConstantObj && sleepSensor.getStageConstantObj()
      const raw = (sleepSensor.getStage && sleepSensor.getStage()) || []
      stages = (raw || []).map((s) => ({
        model: s.model,
        start: s.start,
        stop: s.stop,
        label:
          consts && s.model === consts.WAKE_STAGE
            ? 'wake'
            : consts && s.model === consts.REM_STAGE
              ? 'rem'
              : consts && s.model === consts.LIGHT_STAGE
                ? 'light'
                : consts && s.model === consts.DEEP_STAGE
                  ? 'deep'
                  : 'stage',
      }))
    } catch (_) {}
    if (stages.length > 40) stages = stages.slice(0, 40)
  }

  const naps = normalizeNaps(safe(() => (sleepSensor.getNap && sleepSensor.getNap()) || [], []))
  const totalMin = asInt(info.totalTime, 0)
  const napMin = naps.reduce((a, n) => a + (n.length || 0), 0)

  return {
    score: info.score,
    total_min: totalMin > 0 ? totalMin : null,
    deep_min: info.deepTime != null ? asInt(info.deepTime, null) : null,
    light_min: info.lightTime != null ? asInt(info.lightTime, null) : null,
    rem_min: info.remTime != null ? asInt(info.remTime, null) : null,
    start_min: info.startTime,
    end_min: info.endTime,
    stages,
    naps,
    nap_min: napMin > 0 ? napMin : 0,
    sleeping_status: safe(() => sleepSensor.getSleepingStatus && sleepSensor.getSleepingStatus(), null),
  }
}

function collectHeart(full) {
  const hr = new HeartRate()
  const out = {
    last: safe(() => hr.getLast(), null),
    resting: safe(() => hr.getResting && hr.getResting(), null),
  }
  if (full) {
    const todayRaw = safe(() => (hr.getToday && hr.getToday()) || [], [])
    out.today_min = downsample(todayRaw, 180)
    out.today_count = Array.isArray(todayRaw) ? todayRaw.length : 0
    out.daily_summary = safe(() => (hr.getDailySummary && hr.getDailySummary()) || null, null)
    // Max / average from today's minute series when firmware exposes it
    const nums = (todayRaw || []).filter((n) => typeof n === 'number' && n > 0)
    if (nums.length) {
      out.max = Math.max.apply(null, nums)
      out.avg = Math.round(nums.reduce((a, b) => a + b, 0) / nums.length)
    }
  }
  return out
}

function collectStress(full) {
  const st = new Stress()
  const cur = safe(() => st.getCurrent(), {}) || {}
  const out = { value: cur.value, time: cur.time }
  if (full) {
    out.today_by_hour = safe(() => st.getTodayByHour && st.getTodayByHour(), null)
    out.last_week = safe(() => st.getLastWeek && st.getLastWeek(), null)
  }
  return out
}

function collectSpo2(full) {
  const bo = new BloodOxygen()
  const cur = safe(() => bo.getCurrent(), {}) || {}
  const out = { value: cur.value, time: cur.time, retCode: cur.retCode }
  if (full) {
    const lastDay = safe(() => bo.getLastDay && bo.getLastDay(), []) || []
    const nums = (lastDay || []).filter((n) => typeof n === 'number' && n > 0)
    out.last_day_avg = nums.length
      ? Math.round(nums.reduce((a, b) => a + b, 0) / nums.length)
      : null
    out.last_day = downsample(nums, 24)
    const few = safe(() => (bo.getLastFewHour && bo.getLastFewHour(6)) || [], []) || []
    if (few.length) {
      out.recent = few.slice(0, 48).map((s) => ({
        spo2: s && s.spo2 != null ? s.spo2 : null,
        time: s && s.time != null ? s.time : null,
      }))
    }
  }
  return out
}

/** Recent workouts / HR zones — absent when firmware lacks Workout API. */
function collectWorkouts(full) {
  if (!full) return { workouts: null, hr_zones: null, capabilities: { workouts: false } }
  let workouts = null
  let hr_zones = null
  let ok = false
  try {
    let WorkoutCtor = null
    try {
      const mod = require('@zos/sensor')
      WorkoutCtor = mod && mod.Workout ? mod.Workout : null
    } catch (_) {
      WorkoutCtor = null
    }
    if (!WorkoutCtor) {
      return { workouts: null, hr_zones: null, capabilities: { workouts: false } }
    }
    const w = new WorkoutCtor()
    const hist = safe(() => (w.getHistory && w.getHistory()) || [], []) || []
    if (Array.isArray(hist) && hist.length) {
      workouts = hist.slice(0, 24).map((item) => {
        const it = item || {}
        return {
          type: it.type != null ? it.type : it.name != null ? it.name : it.sport != null ? it.sport : null,
          start: it.start != null ? it.start : it.startTime != null ? it.startTime : null,
          end: it.end != null ? it.end : it.endTime != null ? it.endTime : null,
          duration_min:
            it.duration_min != null
              ? asInt(it.duration_min, null)
              : it.duration != null
                ? asInt(it.duration, null)
                : it.minute != null
                  ? asInt(it.minute, null)
                  : null,
          calories: it.calories != null ? asInt(it.calories, null) : null,
          distance_m: it.distance != null ? asInt(it.distance, null) : null,
        }
      })
      ok = workouts.length > 0
    }
    const zones = safe(() => (w.getUserHrZoneSettings && w.getUserHrZoneSettings()) || null, null)
    if (zones && typeof zones === 'object') {
      hr_zones = {
        type: zones.type != null ? zones.type : null,
        rest: zones.rest != null ? zones.rest : null,
        range: Array.isArray(zones.range) ? zones.range.slice(0, 8) : null,
      }
    }
  } catch (_) {
    workouts = null
    hr_zones = null
  }
  return { workouts, hr_zones, capabilities: { workouts: ok } }
}

/** Optional temperature — only if runtime exposes a sensor. Never invent values. */
function collectTemperature() {
  const caps = { temperature: false }
  let temperature = null
  try {
    // Some firmwares expose Temperature; others do not — probe safely.
    // eslint-disable-next-line no-undef
    const SensorCtor =
      typeof Temperature !== 'undefined'
        ? Temperature
        : null
    if (!SensorCtor) {
      try {
        const mod = require('@zos/sensor')
        if (mod && mod.Temperature) {
          const t = new mod.Temperature()
          const cur = safe(() => (t.getCurrent && t.getCurrent()) || t.getLast && t.getLast(), null)
          if (cur != null && typeof cur === 'object') {
            temperature = {
              celsius: cur.value != null ? cur.value : cur.celsius != null ? cur.celsius : null,
              time: cur.time != null ? cur.time : null,
            }
            caps.temperature = temperature.celsius != null
          } else if (typeof cur === 'number') {
            temperature = { celsius: cur }
            caps.temperature = true
          }
        }
      } catch (_) {}
      return { temperature, capabilities: caps }
    }
    const t = new SensorCtor()
    const cur = safe(() => (t.getCurrent && t.getCurrent()) || null, null)
    if (cur != null) {
      temperature = {
        celsius: cur.value != null ? cur.value : cur.celsius != null ? cur.celsius : null,
        time: cur.time != null ? cur.time : null,
      }
      caps.temperature = temperature.celsius != null
    }
  } catch (_) {}
  return { temperature, capabilities: caps }
}

function collectWeather() {
  const w = new Weather()
  const raw =
    safe(() => w.getForecastWeather && w.getForecastWeather(), null) ||
    safe(() => w.getForecast && w.getForecast(), null)
  if (!raw) return null
  const fd = raw.forecastData || {}
  const today = fd.data && fd.data[0] ? fd.data[0] : null
  return {
    city: raw.cityName || null,
    today_high: today ? today.high : null,
    today_low: today ? today.low : null,
  }
}

function collectDeviceMeta() {
  const meta = {}
  try {
    const info = getDeviceInfo && getDeviceInfo()
    if (info) {
      meta.device_name = info.deviceName || info.name || null
      meta.screen = info.width && info.height ? `${info.width}x${info.height}` : null
    }
  } catch (_) {}
  try {
    const t = new Time()
    meta.time = {
      hour: safe(() => t.getHours(), null),
      minute: safe(() => t.getMinutes(), null),
    }
  } catch (_) {}
  try {
    const p = getProfile && getProfile()
    if (p) {
      meta.profile = {
        age: p.age != null ? p.age : null,
        gender: p.gender != null ? p.gender : null,
      }
    }
  } catch (_) {}
  return meta
}

/**
 * @param {'lean'|'full'} [mode]
 */
export function buildHealthSnapshot(mode) {
  const full = mode !== 'lean'
  const sleep = safe(() => collectSleep(full), {
    score: null,
    total_min: null,
    deep_min: null,
    start_min: null,
    end_min: null,
    stages: [],
    naps: [],
    nap_min: 0,
    sleeping_status: null,
  })

  const heart = safe(() => collectHeart(full), { last: null, resting: null })
  const stress = safe(() => collectStress(full), {})
  const spo2 = safe(() => collectSpo2(full), {})
  const tempPack = safe(collectTemperature, { temperature: null, capabilities: { temperature: false } })
  const workoutPack = safe(() => collectWorkouts(full), {
    workouts: null,
    hr_zones: null,
    capabilities: { workouts: false },
  })

  const capabilities = {
    sleep: !!(sleep && (sleep.total_min != null || (sleep.naps && sleep.naps.length))),
    heart: !!(heart && (heart.last != null || heart.resting != null)),
    heart_series: !!(heart && heart.today_min && heart.today_min.length),
    stress: stress && stress.value != null,
    spo2: spo2 && spo2.value != null && Number(spo2.value) > 0,
    temperature: !!(tempPack && tempPack.capabilities && tempPack.capabilities.temperature),
    workouts: !!(workoutPack && workoutPack.capabilities && workoutPack.capabilities.workouts),
    steps: false,
    calorie: false,
    distance: false,
    pai: false,
    stand: false,
    battery: false,
    fat_burn: false,
  }

  let activity = {}
  try {
    const step = new Step()
    activity = {
      steps: step.getCurrent(),
      target: step.getTarget && step.getTarget(),
    }
    capabilities.steps = activity.steps != null
    const extraKeys = ['sitting_min', 'sedentary_min', 'sit_minutes', 'sitting_minutes', 'inactive_min']
    for (let i = 0; i < extraKeys.length; i++) {
      const key = extraKeys[i]
      try {
        const fn = step[key] || (step.get && step.get(key))
        if (typeof fn === 'function') {
          const n = asInt(fn.call(step), null)
          if (n != null) activity[key] = n
        }
      } catch (_) {}
    }
  } catch (_) {}

  let sitting = null
  try {
    const sitKeys = ['sitting_min', 'sedentary_min', 'sit_minutes', 'sitting_minutes', 'inactive_min']
    for (let i = 0; i < sitKeys.length; i++) {
      const n = asInt(activity[sitKeys[i]], null)
      if (n != null) {
        sitting = { minutes: n }
        break
      }
    }
  } catch (_) {}

  let calorie = {}
  try {
    const c = new Calorie()
    calorie = {
      kcal: c.getCurrent(),
      target: c.getTarget && c.getTarget(),
    }
    capabilities.calorie = calorie.kcal != null
  } catch (_) {}

  let distance = {}
  try {
    const d = new Distance()
    distance = { meters: d.getCurrent() }
    capabilities.distance = distance.meters != null
  } catch (_) {}

  let pai = {}
  try {
    const p = new Pai()
    pai = {
      today: p.getToday && p.getToday(),
      total: p.getTotal && p.getTotal(),
      last_week: full && p.getLastWeek ? safe(() => p.getLastWeek(), null) : null,
    }
    capabilities.pai = pai.today != null
  } catch (_) {}

  let stand = {}
  try {
    const s = new Stand()
    stand = {
      hours: s.getCurrent(),
      target: s.getTarget && s.getTarget(),
    }
    capabilities.stand = stand.hours != null
  } catch (_) {}

  let battery = {}
  try {
    const b = new Battery()
    battery = { pct: b.getCurrent() }
    capabilities.battery = battery.pct != null
  } catch (_) {}

  let fat_burn = {}
  try {
    const fb = new FatBurning()
    fat_burn = {
      minutes: fb.getCurrent && fb.getCurrent(),
      target: fb.getTarget && fb.getTarget(),
    }
    capabilities.fat_burn = fat_burn.minutes != null
  } catch (_) {}

  const clock = watchClock()
  const rich =
    full &&
    ((heart && heart.today_min && heart.today_min.length) ||
      (workoutPack && workoutPack.workouts && workoutPack.workouts.length) ||
      (sleep && sleep.stages && sleep.stages.length))
  const out = {
    dump: full ? (rich ? 'processed_v2' : 'processed_v1') : 'lean_v1',
    captured_at: clock.captured_at,
    local_date: clock.local_date,
    tz_offset_min: clock.tz_offset_min,
    sleep,
    heart,
    activity,
    calorie,
    distance,
    spo2,
    stress,
    pai,
    stand,
    battery,
    fat_burn,
    sitting,
    capabilities,
  }
  if (tempPack && tempPack.temperature) {
    out.temperature = tempPack.temperature
  }
  if (workoutPack && workoutPack.workouts && workoutPack.workouts.length) {
    out.workouts = workoutPack.workouts
  }
  if (workoutPack && workoutPack.hr_zones) {
    out.hr_zones = workoutPack.hr_zones
    if (out.heart && typeof out.heart === 'object') {
      out.heart.hr_zones = workoutPack.hr_zones
    }
  }
  if (full) {
    out.weather = safe(collectWeather, null)
    out.meta_device = safe(collectDeviceMeta, {})
  }
  return out
}

export function sleepDisplayMinutes(health) {
  const s = (health && health.sleep) || {}
  const main = asInt(s.total_min, 0) || 0
  const naps = asInt(s.nap_min, 0) || 0
  return main + naps
}

export function snapshotSummary(h) {
  const steps = (h.activity && h.activity.steps) || 0
  const sleep = sleepDisplayMinutes(h)
  const kcal = (h.calorie && h.calorie.kcal) || 0
  const hr = (h.heart && h.heart.last) || '—'
  const spo2 = h.spo2 && h.spo2.value != null && Number(h.spo2.value) > 0 ? h.spo2.value : '—'
  const stress = h.stress && h.stress.value != null ? h.stress.value : '—'
  const temp =
    h.temperature && h.temperature.celsius != null
      ? `${h.temperature.celsius}°`
      : h.capabilities && h.capabilities.temperature === false
        ? 'n/a'
        : '—'
  return `${steps}st · ${formatHoursMins(sleep)} · HR ${hr}\nSpO₂ ${spo2} · stress ${stress} · T ${temp}`
}
