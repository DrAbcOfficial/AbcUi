#pragma once

// framework::Assets - abcui/ asset install checks + shared path prefixes.
// Plan 05 §1: assets live in <mod>/svencoop/abcui/ (rml/rcss/img/lang).
// File reads themselves go through AbcFileInterface (mod-dir resolution), so
// this module only owns existence checks and the canonical prefix constants.

namespace framework {

namespace Assets {

// Canonical asset-root prefix as referenced from RML/RCSS.
constexpr const char* kRootPrefix = "abcui/";

// LoadClient time: verify the installed assets, warn (once) about the ones
// the demo needs when missing (plugin loads even without them).
void verifyInstalled();

}

}
