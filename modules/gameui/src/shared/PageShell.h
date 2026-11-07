#pragma once

// Shared page-document mechanics for the abcui.gameui projections. A page
// keeps its model, its rendering and its callbacks; this header owns the
// menu-state/paint subscriptions, the document descriptor and the derived
// wants/dismiss rules that every page repeated.

#include "PageShared.h"

namespace pageshell {

inline bool wants(const AbcUiMenuState *state, int32_t page) {
  return state && state->up && state->page == page;
}

inline void dismiss(AbcUiDocument *page) {
  if (page && Api->documents->visible(page))
    Api->documents->hide(page);
}

inline AbcUiSubscription subscribeState(AbcUiEventSink sink, void *userdata) {
  return Api->events->subscribe(ABCUI_CHANNEL_MENU_STATE, 0, sink, userdata);
}

inline AbcUiSubscription subscribePaint(AbcUiEventSink sink, void *userdata) {
  return Api->events->subscribe(ABCUI_CHANNEL_MENU_PAINT, 0, sink, userdata);
}

inline void unsubscribe(AbcUiSubscription &subscription) {
  if (subscription) {
    Api->events->unsubscribe(subscription);
    subscription = 0;
  }
}

// Fills the menu-page descriptor the host validates. onShow may be null.
inline AbcUiDocumentDesc menuDocument(
    const char *name, const char *path, uint32_t flags, int32_t zIndex,
    void(ABCUI_CALL *onReady)(void *), void(ABCUI_CALL *onShow)(void *),
    void(ABCUI_CALL *onHide)(void *), void(ABCUI_CALL *onClose)(void *)) {
  AbcUiDocumentDesc desc{};
  desc.structSize = sizeof(desc);
  desc.name = name;
  desc.path = path;
  desc.flags = flags;
  desc.zIndex = zIndex;
  desc.userdata = nullptr;
  desc.onReady = onReady;
  desc.onShow = onShow;
  desc.onHide = onHide;
  desc.onClose = onClose;
  return desc;
}

inline AbcUiDocument *create(const AbcUiDocumentDesc &desc) {
  return Api->documents->create(&desc);
}

inline uint64_t addListener(AbcUiElement *element, const char *type,
                            AbcUiElementListener listener) {
  return element ? Api->ui->elementAddListener(element, type, listener, nullptr)
                 : 0;
}

inline void removeListener(AbcUiElement *element, const char *type,
                           uint64_t &binding) {
  if (element && binding) {
    Api->ui->elementRemoveListener(element, type, binding);
    binding = 0;
  }
}

} // namespace pageshell
