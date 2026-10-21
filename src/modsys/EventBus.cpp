// framework::EventBus implementation - see EventBus.h.
#include "EventBus.h"

namespace framework {
namespace {

constexpr EventSubscription kNoSubscription = 0;

} // namespace

EventBus::Channel *EventBus::find(const char *channel) {
  const auto it = Channels.find(channel);
  return it == Channels.end() ? nullptr : &it->second;
}

const EventBus::Channel *EventBus::find(const char *channel) const {
  const auto it = Channels.find(channel);
  return it == Channels.end() ? nullptr : &it->second;
}

EventSubscription EventBus::subscribe(const char *channel,
                                      EventPriority priority, EventSinkFn sink,
                                      void *userdata) {
  if (!channel || !sink)
    return kNoSubscription;
  Channel &target = Channels[channel];
  DataList &list = target.data[priority];
  const auto handle = list.append(
      [sink, userdata](const void *payload) { sink(userdata, payload); });
  ++target.count;
  const EventSubscription token = NextToken++;
  Record &record = Records[token];
  record.channel = channel;
  record.priority = priority;
  record.isVeto = false;
  record.dataHandle = handle;
  return token;
}

EventSubscription EventBus::subscribeVeto(const char *channel,
                                          EventPriority priority,
                                          EventVetoFn sink, void *userdata) {
  if (!channel || !sink)
    return kNoSubscription;
  Channel &target = Channels[channel];
  VetoList &list = target.veto[priority];
  const auto handle = list.append([sink, userdata](const void *payload) {
    return sink(userdata, payload);
  });
  ++target.count;
  const EventSubscription token = NextToken++;
  Record &record = Records[token];
  record.channel = channel;
  record.priority = priority;
  record.isVeto = true;
  record.vetoHandle = handle;
  return token;
}

void EventBus::unsubscribe(EventSubscription subscription) {
  if (subscription == kNoSubscription)
    return;
  const auto it = Records.find(subscription);
  if (it == Records.end())
    return;
  Record &record = it->second;
  if (Channel *target = find(record.channel.c_str())) {
    --target->count;
    if (record.isVeto) {
      const auto bucket = target->veto.find(record.priority);
      if (bucket != target->veto.end())
        bucket->second.remove(record.vetoHandle);
    } else {
      const auto bucket = target->data.find(record.priority);
      if (bucket != target->data.end())
        bucket->second.remove(record.dataHandle);
    }
  }
  delete record.wrapper;
  Records.erase(it);
}

void EventBus::ownWrapper(EventSubscription token, FnWrapperBase *wrapper) {
  const auto it = Records.find(token);
  if (it == Records.end()) {
    delete wrapper;
    return;
  }
  it->second.wrapper = wrapper;
}

void EventBus::publishRaw(const char *channel, const void *payload) {
  const Channel *target = find(channel);
  if (!target)
    return;
  for (const auto &bucket : target->data) {
    const DataList &list = bucket.second;
    list.forEach(
        [&](const DataList::Handle &, const DataList::Callback &callback) {
          callback(payload);
        });
  }
}

bool EventBus::publishVetoRaw(const char *channel, const void *payload) {
  const Channel *target = find(channel);
  if (!target)
    return false;
  bool vetoed = false;
  for (const auto &bucket : target->veto) {
    if (vetoed)
      break;
    const VetoList &list = bucket.second;
    list.forEachIf(
        [&](const VetoList::Handle &, const VetoList::Callback &callback) {
          if (callback(payload)) {
            vetoed = true;
            return false;
          }
          return true;
        });
  }
  return vetoed;
}

size_t EventBus::subscriberCount(const char *channel) const {
  const Channel *target = find(channel);
  return target ? target->count : 0;
}

EventBus &eventBus() {
  static EventBus instance;
  return instance;
}

} // namespace framework
