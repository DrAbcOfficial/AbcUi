// Offline acceptance for the native donor word: g_PlayerExtraInfo + 0x54
// packs "showextras << 16 | extraslevel". The store must decode the native
// word itself (no ScoreInfo wire gate, which lags the client write by one
// message), ignore the padding bits and reject unknown levels.
#include "data/PlayerResource.h"
#include "framework/TestHarness.h"

using playerres::Donor;
using playerres::decodeDonor;

int main() {
  TEST_CASE("donor native word decode");
  {
    CHECK(decodeDonor(0) == Donor::None);              // slot never saw extras
    CHECK(decodeDonor(0x10000) == Donor::None);        // shown, level 0
    CHECK(decodeDonor(0x10001) == Donor::ElectricCrowbar);
    CHECK(decodeDonor(0x10002) == Donor::GoldedUzi);
    CHECK(decodeDonor(0x10003) == Donor::GoldedDollar);
    CHECK(decodeDonor(0x10004) == Donor::Tester);
    CHECK(decodeDonor(0x10005) == Donor::Artist);
    CHECK(decodeDonor(0x10006) == Donor::Developer);
  }

  TEST_CASE("hidden, banned and unknown levels decode to none");
  {
    CHECK(decodeDonor(0x00001) == Donor::None); // hidden donor (no show bit)
    CHECK(decodeDonor(0x00006) == Donor::None); // hidden developer
    CHECK(decodeDonor(0x1FFFF) == Donor::None); // banned (-1)
    CHECK(decodeDonor(0x10007) == Donor::None); // level outside 1..6
    CHECK(decodeDonor(0x100FF) == Donor::None);
  }

  TEST_CASE("padding bits above the show flag are ignored");
  {
    CHECK(decodeDonor(0xFF010001) == Donor::ElectricCrowbar);
    CHECK(decodeDonor(static_cast<int32_t>(0x80010006u)) == Donor::Developer);
  }

  TEST_CASE("decoded values keep the public 0x10000 + kind form");
  {
    CHECK(static_cast<int32_t>(decodeDonor(0x10001)) == 0x10001);
    CHECK(static_cast<int32_t>(decodeDonor(0x10006)) == 0x10006);
  }

  return abctest::finish("abc_ui_player_flags");
}
