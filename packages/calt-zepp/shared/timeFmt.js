/**
 * Hours + minutes labels for watch UI (values stay stored as minutes).
 */
export function formatHoursMins(totalMinutes) {
  if (totalMinutes == null || Number.isNaN(Number(totalMinutes))) return '—'
  const n = Math.max(0, Math.round(Number(totalMinutes)))
  const h = Math.floor(n / 60)
  const m = n % 60
  const hours = h === 1 ? '1 hour' : h + ' hours'
  const mins = m === 1 ? '1 min' : m + ' mins'
  if (h === 0) return hours + ' ' + mins
  if (m === 0) return hours
  return hours + ' ' + mins
}
