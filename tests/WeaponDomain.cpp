// Contract test for the engine prediction weapon domain.
//
// svencoop_sdk/common/entity_state.h packs weapon_data[256]; the prediction
// diff indexes that array by the server weapon id. The registration boundary
// must accept the full SDK domain (including ids 64..255) and reject every
// id that would index past it.
#include "data/WeaponDomain.h"

#include "framework/TestHarness.h"

int main() {
  TEST_CASE("prediction domain matches the SDK array");
  CHECK(weapondomain::kPredictionWeaponCount == 256);

  TEST_CASE("ids 1..255 are accepted");
  CHECK(weapondomain::validWeaponId(1));
  CHECK(weapondomain::validWeaponId(63));
  CHECK(weapondomain::validWeaponId(64));
  CHECK(weapondomain::validWeaponId(255));

  TEST_CASE("zero, negative and 256+ are rejected");
  CHECK(!weapondomain::validWeaponId(0));
  CHECK(!weapondomain::validWeaponId(-1));
  CHECK(!weapondomain::validWeaponId(256));
  CHECK(!weapondomain::validWeaponId(30000));

  return abctest::finish("abc_ui_weapon_domain");
}
