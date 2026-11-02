// SDK claims/facts sections (see module_api.h) on top of the engine-free
// arbitration and fact stores.
#include "ApiTable.h"

#include <abcui/module_api.h>

#include "Arbitration.h"
#include "Facts.h"

namespace framework {
namespace ClaimsApi {
namespace {

bool validDesc(const AbcUiClaimDesc *desc) {
  return desc && desc->structSize >= sizeof(AbcUiClaimDesc) &&
         desc->role >= 0 && desc->role <= ABCUI_CLAIM_GATE;
}

AbcUiClaim apiClaimCommand(const char *name, const AbcUiClaimDesc *desc) {
  if (!name || !validDesc(desc))
    return 0;
  return arbitration::claimCommand(
      name, static_cast<arbitration::Role>(desc->role), desc->priority,
      desc->canHandle, desc->handle, desc->userdata);
}

AbcUiClaim apiClaimDomain(const char *domain, const AbcUiClaimDesc *desc) {
  if (!domain || !validDesc(desc))
    return 0;
  return arbitration::claim(domain, static_cast<arbitration::Role>(desc->role),
                            desc->priority, desc->canHandle, desc->handle,
                            desc->userdata);
}

void apiRelease(AbcUiClaim claim) {
  arbitration::release(claim);
}

void apiSetActive(AbcUiClaim claim, int32_t active) {
  arbitration::setActive(claim, active != 0);
}

const AbcUiClaimsApi kClaimsApi = {
    sizeof(AbcUiClaimsApi), apiClaimCommand, apiClaimDomain, apiRelease,
    apiSetActive,
};

} // namespace

const AbcUiClaimsApi *get() {
  return &kClaimsApi;
}

} // namespace ClaimsApi

namespace FactsApi {
namespace {

int32_t apiFactGet(const char *name, void *out, uint32_t outSize) {
  return facts::get(name, out, outSize) ? 1 : 0;
}

uint32_t apiFactGeneration(const char *name) {
  return facts::generation(name);
}

const AbcUiFactsApi kFactsApi = {
    sizeof(AbcUiFactsApi),
    apiFactGet,
    apiFactGeneration,
};

} // namespace

const AbcUiFactsApi *get() {
  return &kFactsApi;
}

} // namespace FactsApi
} // namespace framework
