"""Focus QA error codes."""

OK = "OK"
E100 = "E100"  # SoftLand action mismatch
E101 = "E101"  # SoftLand reason mismatch
E110 = "E110"  # SoftLand transport / msg_host
E200 = "E200"  # Arm unit match misfire
E210 = "E210"  # Arm live: listed target not killed
E211 = "E211"  # Arm live: safe target killed
E300 = "E300"  # Gateway timeout / unreachable
E301 = "E301"  # Gateway unexpected response
E302 = "E302"  # Negative: expected reject, got success
E400 = "E400"  # Sync mismatch
E500 = "E500"  # UI load fail
E501 = "E501"  # UI assertion fail
E900 = "E900"  # Unexpected exception

DESCRIPTIONS = {
    OK: "Pass",
    E100: "SoftLand action mismatch",
    E101: "SoftLand reason mismatch",
    E110: "SoftLand msg_host / decide transport error",
    E200: "Arm unit match misfire",
    E210: "Arm live: listed target not killed",
    E211: "Arm live: safe / unlisted target killed",
    E300: "Gateway timeout / unreachable",
    E301: "Gateway unexpected response",
    E302: "Negative test: expected reject but got success",
    E400: "JSON ↔ SQLite sync mismatch",
    E500: "UI page load failed",
    E501: "UI assertion failed",
    E900: "Unexpected exception",
}
