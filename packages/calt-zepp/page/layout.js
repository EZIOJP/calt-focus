/**
 * Shared layout helpers — scale to real watch pixels (T-Rex 3 = 480×480).
 */
import { getDeviceInfo } from '@zos/device'

export function screen() {
  let width = 480
  let height = 480
  try {
    const info = getDeviceInfo()
    width = info.width || width
    height = info.height || height
  } catch (_) {}
  const pad = Math.round(width * 0.06)
  const contentW = width - pad * 2
  return { width, height, pad, contentW, cx: Math.round(width / 2) }
}

export function yFrac(height, frac) {
  return Math.round(height * frac)
}
