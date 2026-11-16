import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(__dirname, "..");
const seed = JSON.parse(fs.readFileSync(path.join(root, "data/host_seed.json"), "utf8"));

function cppStrings(arr) {
  return arr.map((h) => '  "' + h.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"').join(",\n");
}

const hdr =
  "// AUTO-GENERATED from extensions/calt-gate-extension/data/host_seed.json — do not edit.\n" +
  "#pragma once\n#include <string>\n#include <vector>\n\n" +
  "inline const std::vector<std::string>& SeedPornHosts() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings(seed.tiers.PORN_STRICT) +
  "\n  };\n  return k;\n}\n\n" +
  "inline const std::vector<std::string>& SeedSensitiveHosts() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings(seed.tiers.SENSITIVE) +
  "\n  };\n  return k;\n}\n\n" +
  "inline const std::vector<std::string>& SeedPornSuffix() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings(seed.suffixes) +
  "\n  };\n  return k;\n}\n\n" +
  "inline const std::vector<std::string>& SeedPornLabels() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings(seed.host_labels) +
  "\n  };\n  return k;\n}\n\n" +
  "inline const std::vector<std::string>& SeedWatchHosts() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings([...(seed.tiers.WATCH || []), ...(seed.tiers.SOCIAL || [])]) +
  "\n  };\n  return k;\n}\n\n" +
  // SoftLand msg-host allow — mirrors Gate FALLBACK study/trusted hosts (scaler, google, …).
  "inline const std::vector<std::string>& SeedAllowHosts() {\n" +
  "  static const std::vector<std::string> k = {\n" +
  cppStrings([...(seed.tiers.TRUSTED || []), ...(seed.tiers.SEARCH || [])]) +
  "\n  };\n  return k;\n}\n";

// root = .../extensions/calt-gate-extension → repo backend/
const outCpp = path.join(root, "../../backend/calt_msg_host/src/host_seed.generated.h");
fs.mkdirSync(path.dirname(outCpp), { recursive: true });
fs.writeFileSync(outCpp, hdr);
console.log("Wrote", outCpp);
