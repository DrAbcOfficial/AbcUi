// abc_ui_model_preview_camera - the distance the settings page names for
// the player model preview's camera (issue 36 keeps the <model> camera
// explicit, so the page owns it). The engine projects with a fixed
// horizontal field of view, so the distance follows the card's shape: the
// same number of units stays in view from top to bottom whether the card is
// short and wide or nearly square.
#include "ModelPreviewCamera.h"

#include "framework/TestHarness.h"

#include <cmath>
#include <limits>
#include <utility>

int main() {
  using namespace settingspage;
  {
    TEST_CASE("ninety-units-stay-in-view-whatever-the-shape");
    // What a card shows vertically is twice the distance times height over
    // width (a 90 degree field of view): the same for every shape.
    for (const auto shape :
         {std::pair{1200.0f, 540.0f}, std::pair{915.0f, 835.0f},
          std::pair{300.0f, 300.0f}, std::pair{640.0f, 960.0f}}) {
      const float distance = modelPreviewDistance(shape.first, shape.second);
      const float visible = 2.0f * distance * shape.second / shape.first;
      CHECK_NEAR(visible, kModelPreviewVisibleUnits, 1e-3f);
      // The default player model is 72 units tall: four fifths of the card.
      CHECK_NEAR(72.0f / visible, 0.8f, 1e-3f);
    }
  }
  {
    TEST_CASE("the-shape-the-document-was-tuned-for-keeps-its-camera");
    // A short card of this shape gets exactly the 100 the document ships
    // with: there the preview looks as it did.
    CHECK_NEAR(modelPreviewDistance(2000.0f, 900.0f), 100.0f, 1e-3f);
  }
  {
    TEST_CASE("a-wider-card-is-seen-from-further");
    CHECK(modelPreviewDistance(1200.0f, 540.0f) >
          modelPreviewDistance(915.0f, 835.0f));
    CHECK(modelPreviewDistance(915.0f, 835.0f) > 0.0f);
  }
  {
    TEST_CASE("no-layout-keeps-the-documents-camera");
    CHECK_NEAR(modelPreviewDistance(0.0f, 0.0f), kModelPreviewDefaultDistance,
               1e-6f);
    CHECK_NEAR(modelPreviewDistance(-1.0f, 100.0f),
               kModelPreviewDefaultDistance, 1e-6f);
    CHECK_NEAR(modelPreviewDistance(100.0f, 0.0f), kModelPreviewDefaultDistance,
               1e-6f);
    CHECK_NEAR(
        modelPreviewDistance(std::numeric_limits<float>::quiet_NaN(), 100.0f),
        kModelPreviewDefaultDistance, 1e-6f);
  }
  return abctest::finish("abc_ui_model_preview_camera");
}
