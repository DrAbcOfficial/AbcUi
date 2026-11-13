// Pins the UserMsg field quirks that differ from a naive byte read:
// HideHUD is a short token (bits 8/9 included), AmmoX is an absolute count,
// and MapList has a payload-less end opcode.
#include "data/UserMsgFields.h"

#include <abcui/events.h>

#include "framework/TestHarness.h"

int main() {
  TEST_CASE("HideHUD token is a short");
  {
    const unsigned char lowOnly[2] = {0x00, 0x01};
    int token = -1;
    CHECK(netmsg::decodeHideHud(lowOnly, sizeof(lowOnly), token));
    CHECK(token == 256); // HIDEHUD_CROSSHAIR

    const unsigned char both[2] = {0x00, 0x03};
    CHECK(netmsg::decodeHideHud(both, sizeof(both), token));
    CHECK(token == 768); // CROSSHAIR | AMMO

    const unsigned char truncated[1] = {0x01};
    CHECK(!netmsg::decodeHideHud(truncated, sizeof(truncated), token));
  }

  TEST_CASE("the ABI exposes both SDK bits above the first byte");
  CHECK(ABCUI_HIDEBIT_CROSSHAIR == (1 << 8));
  CHECK(ABCUI_HIDEBIT_AMMO == (1 << 9));

  TEST_CASE("AmmoX normalizes the sign like the native HUD");
  {
    int count = -1;
    CHECK(netmsg::normalizeAmmoCount(12, count));
    CHECK(count == 12);
    CHECK(netmsg::normalizeAmmoCount(-12, count));
    CHECK(count == 12);
    CHECK(netmsg::normalizeAmmoCount(0, count));
    CHECK(count == 0);
    CHECK(!netmsg::normalizeAmmoCount(INT_MIN, count));
  }

  TEST_CASE("MapList end opcode is distinct from reset");
  CHECK(netmsg::kMapListEnd == 123);
  CHECK(netmsg::kMapListEnd != 0);

  return abctest::finish("abc_ui_user_msg_fields");
}
