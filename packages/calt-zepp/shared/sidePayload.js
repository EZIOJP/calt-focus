/**
 * Side service replies via ctx.response({ data: payload }).
 * MessageBuilder delivers that envelope to the watch — unwrap once here.
 */
export function sidePayload(res) {
  if (res == null) return {}
  if (typeof res === 'object' && res.data !== undefined && res.data !== null) {
    return res.data
  }
  return res
}
