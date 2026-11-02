#pragma once

// framework::EventBus - named observation channels with priority buckets,
// built on AbcCore's eventpp vocabulary (abc::CallbackList).
//
// The bus is the module-facing observation layer: the framework data layer
// publishes typed occurrences (network messages, user operations, lifecycle
// ticks) and modules subscribe through the C ABI with a priority. It never
// decides ownership - entry points use framework::arbitration instead.
//
// Ordering: buckets dispatch in descending priority (priority > 0 runs before
// the publisher's default 0, < 0 runs after as fallback), and within a bucket
// the eventpp CallbackList keeps registration order. Subscriptions return an
// opaque token; unsubscribe is idempotent. The bus is a process-lifetime
// service: SDK sinks retain their creating module's scope and are revoked
// automatically on failure/final unload, not HUD_Shutdown.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <abc/Events.h>

#include "ModuleResources.h"

namespace framework {

using EventPriority = int;

// Opaque subscription token; 0 is never issued.
using EventSubscription = std::uint64_t;

// Module-facing data sink. The payload pointer is only valid for the call.
using EventSinkFn = void (*)(void *userdata, const void *payload);

class EventBus {
public:
  ~EventBus();
  // Subscribes a module-facing sink. Same priority keeps registration order.
  EventSubscription subscribe(const char *channel, EventPriority priority,
                              EventSinkFn sink, void *userdata);
  // Idempotent; unknown tokens are ignored.
  void unsubscribe(EventSubscription subscription);

  // C++ sugar: subscribes a callable taking the typed payload. The closure
  // is owned by the bus and destroyed on unsubscribe.
  template <typename T, typename Fn>
  EventSubscription subscribeFn(const char *channel, EventPriority priority,
                                Fn &&fn) {
    using Wrapper = FnWrapper<T, std::decay_t<Fn>>;
    auto wrapper = std::make_unique<Wrapper>(std::forward<Fn>(fn));
    const EventSubscription token =
        subscribe(channel, priority, &Wrapper::thunk, wrapper.get());
    ownWrapper(token, std::move(wrapper));
    return token;
  }

  // Dispatches a data channel; the payload stays caller-owned.
  template <typename T> void publish(const char *channel, const T &payload) {
    publishRaw(channel, static_cast<const void *>(&payload));
  }
  void publishRaw(const char *channel, const void *payload);

  // Debug/introspection helper: total sinks registered on a channel.
  size_t subscriberCount(const char *channel) const;

private:
  struct FnWrapperBase {
    virtual ~FnWrapperBase() = default;
    virtual void invoke(const void *payload) = 0;
  };

  template <typename T, typename Fn> struct FnWrapper final : FnWrapperBase {
    FnWrapper(Fn &&fn_) : fn(std::move(fn_)) {}
    void invoke(const void *payload) override {
      fn(*static_cast<const T *>(payload));
    }
    static void thunk(void *userdata, const void *payload) {
      static_cast<FnWrapper *>(userdata)->invoke(payload);
    }
    Fn fn;
  };

  using DataList = abc::CallbackList<void(const void *)>;

  struct Channel {
    // Descending priority order.
    std::map<EventPriority, DataList, std::greater<EventPriority>> data;
    size_t count = 0;
  };

  struct Record {
    std::string channel;
    EventPriority priority = 0;
    DataList::Handle dataHandle;
    std::unique_ptr<FnWrapperBase> wrapper; // set through ownWrapper
    module_resources::Registration lifetime;
  };

  Channel *find(const char *channel);
  const Channel *find(const char *channel) const;
  void ownWrapper(EventSubscription token,
                  std::unique_ptr<FnWrapperBase> wrapper);

  std::map<std::string, Channel, std::less<>> Channels;
  std::map<EventSubscription, Record> Records;
  EventSubscription NextToken = 1;
};

// Process-wide bus (function-local static, per the AGENTS.md service rule).
EventBus &eventBus();

} // namespace framework
