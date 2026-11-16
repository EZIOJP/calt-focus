import * as sw from "./sw_v2.js";
import { decide } from "./decision_engine.js";
import { GATE_V2 } from "./gate_config.js";
import { hostTier } from "./host_intel.js";
import { unwrapDestinationUrl } from "./unwrap.js";

const g = typeof globalThis !== "undefined" ? globalThis : self;
g.CaltGateV2 = {
  GATE_V2,
  init: sw.gateV2Init,
  onNav: sw.gateV2OnNav,
  handleMessage: sw.gateV2HandleMessage,
  lockedUrl: sw.gateV2LockedUrl,
  watchOnLoad: sw.gateV2WatchOnLoad,
  decide,
  hostTier: hostTier,
  unwrap: unwrapDestinationUrl,
};
