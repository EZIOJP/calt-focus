/**
 * Settings — host + queue + log. Health dump only.
 */
import { createWidget, widget, prop } from '@zos/ui'
import { back } from '@zos/router'
import { onGesture, offGesture, GESTURE_RIGHT } from '@zos/interaction'
import { screen } from './layout'
import {
  autoSyncEnabled,
  autoSyncHours,
  setAutoSyncEnabled,
  setAutoSyncHours,
  applyAutoAlarm,
} from './autoAlarm'

Page({
  onInit() {
    onGesture((event) => {
      if (event === GESTURE_RIGHT) {
        back()
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
    const S = screen()
    const { width, height } = S
    createWidget(widget.FILL_RECT, {
      x: 0,
      y: 0,
      w: width,
      h: height,
      color: 0x000000,
    })

    const btnH = Math.round(height * 0.16)
    const btnW = Math.round(width * 0.72)
    const x = Math.round((width - btnW) / 2)
    let y = Math.round(height * 0.28)

    const paint = () => {
      if (this.autoBtn) {
        this.autoBtn.setProperty(prop.TEXT, autoSyncEnabled() ? 'Auto' : 'Manual')
      }
      if (this.hoursBtn) {
        this.hoursBtn.setProperty(prop.TEXT, `${autoSyncHours()}h`)
      }
    }

    this.autoBtn = createWidget(widget.BUTTON, {
      x,
      y,
      w: btnW,
      h: btnH,
      radius: Math.round(btnH / 2),
      text: autoSyncEnabled() ? 'Auto' : 'Manual',
      color: 0xffffff,
      text_size: Math.round(width * 0.06),
      normal_color: 0x111111,
      press_color: 0x2a2a2a,
      click_func: () => {
        setAutoSyncEnabled(!autoSyncEnabled())
        applyAutoAlarm()
        paint()
      },
    })
    y += btnH + Math.round(height * 0.04)
    this.hoursBtn = createWidget(widget.BUTTON, {
      x,
      y,
      w: btnW,
      h: btnH,
      radius: Math.round(btnH / 2),
      text: `${autoSyncHours()}h`,
      color: 0xffffff,
      text_size: Math.round(width * 0.06),
      normal_color: 0x111111,
      press_color: 0x2a2a2a,
      click_func: () => {
        const cur = autoSyncHours()
        const next = cur >= 12 ? 1 : cur === 1 ? 3 : cur === 3 ? 6 : cur === 6 ? 12 : 3
        setAutoSyncHours(next)
        applyAutoAlarm()
        paint()
      },
    })
  },
})
