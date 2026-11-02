#pragma once

// data::textfeed - TextMsg router: splits the vanilla text stream into the
// killfeed and the notice domains. Ported from the g_EventTextMsg handler in
// ABCEnchance src/hud/Viewport.cpp (batch-A half; ScoreInfo-style targets
// chain through untouched).
//
// Routing (old precedence, first claim consumes the message):
//   1. target 1 (PRINTNOTIFY) kill patterns:
//      " committed suicide." / " was killed by a " / " : X : " -> KillFeed
//   2. target 1 or 4 (PRINTCENTER): localized "#token" resolution +
//      printf-style parameter substitution -> Notice
// Everything else chains to the original TextMsg handler.

namespace textfeed {

// TextMsg target values (old HUDNOTICE enum) - also the notice panel keys.
constexpr int kPrintNotify = 1;
constexpr int kPrintCenter = 4;

// Routes one TextMsg. Returns true when an arbitration claim handled it (the
// native message may be swallowed); false leaves the message to the engine.
bool route(int target, const char *msg, const char *param1, const char *param2,
           const char *param3, const char *param4);

} // namespace textfeed
