#pragma once
// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>

#include <map>
#include <string>
#include <vector>

// TODO(josh): split into fusebus_common.h so we don't include fuse stuff
// into the client header.
#include "fusebus/fusebus.h"

namespace fusebus {

// Message buffers are cast to this type for convenient and readible access to
// metadata fields and data.
struct RecvMessage {
  MessageMeta meta;
  uint8_t data[];
};

// Function time for subscription callbacks
typedef std::function<void(const RecvMessage&)> RawCallback;

// Convenience type for ensuring that user's message data has proper prefix
// buffer to avoid iov copy during publish.
template <typename T>
struct Publishable {
  uint64_t write_type;
  uint64_t channelno;
  T msg;
};

inline void JoinHelper(std::ostream* out, const std::string glue) {}

template <typename Arg0, typename... Args>
inline void JoinHelper(std::ostream* out, const std::string glue,
                       const Arg0& head, Args&&... tail) {
  *out << glue << head;
  JoinHelper(out, glue, std::forward<Args>(tail)...);
}

template <typename Arg0, typename... Args>
inline std::string Join(const std::string glue, const Arg0& head,
                        Args&&... tail) {
  std::stringstream stream;
  stream << head;
  JoinHelper(&stream, glue, std::forward<Args>(tail)...);
  return stream.str();
}

// Information about a known channel to which messages are published on the bus
struct ClientChannel {
  uint64_t id;       //< channel id from server
  std::string name;  //< string name of the channel

  // Subscriptions that match this channel
  linked::List links;
};

// Associates a channel pattern with an action to take whenever a message is
// received on that channel.
struct ClientSubscription {
  int id;  //< Subscription id from the server
  std::unique_ptr<re2::RE2> channel_pattern;
  RawCallback callback;

  linked::Node node;   // Position in list of all subscriptions
  linked::List links;  // Channels that match this subscription
};

// Links a channel to a subscription. Used to maintain a sparse bidirectional
// mapping of channels to subscriptions whose patterns match them.
struct ChannelLink {
  ClientChannel* chan;       //< the channel in the link
  ClientSubscription* subs;  //< the subscription in the link
  linked::Node subs_node;    //< location in subscription's list of links
  linked::Node chan_node;    //< location in channel's list of links
};

// Simple client-side manager for access to a fusebus instance.
class Fusebus {
 public:
  // Reserves storage for channels and opens the busfile.
  Fusebus(const std::string& busdir);  // NOLINT(runtime/explicit)

  // Unsubscribe and free any outstanding subscriptions.
  ~Fusebus();

  // Add a new channel to the server (no-op if it's already added)
  int WriteChannel(const std::string& channel);

  // Read the list of channels from the server. Update our local registry of
  // channel objects, and match any outstanding subscriptions against these
  // new channel names.
  int ReadChannels();

  // Retrieve the channel id for the given channel.
  int GetChannel(const std::string& channel, uint64_t* id);

  // Subscribe a callback to a pattern of string channel names. Optionally
  // return a pointer to the subscription object.
  int Subscribe(const std::string& channel_pattern, const RawCallback& callback,
                ClientSubscription** out);

  // Remove a subscription and release any resources that were consumbed by it.
  int Unsubscribe(ClientSubscription* subs);

  // Publish a raw buffer to the bus.
  int Publish(const uint64_t channelno, uint8_t* buf, uint64_t bufsize);

  // Publish a raw buffer to the bus. Note that the data buffer *MUST* contain
  // 16 bytes of unused padding before the data that is to be published.
  int PublishPad(const uint64_t channelno, uint8_t* buf, uint64_t bufsize);

  // Publish a raw struct as a message to the bus.
  template <typename T>
  int Publish(uint64_t channelno, const Publishable<T>& msg);

  // Return the filedescriptor for the opened busfile. This file descriptor may
  // be used in select/poll/epoll and will be ready-for-read when there is a
  // message waiting for this client.
  int GetFileno();

  // Read a one or more message buffers from the server into our client
  // buffer and then dispatch them to all subscribers.
  int Handle();

 private:
  int bus_fd_;
  int chan_fd_;
  std::string busdir_;
  std::vector<char> buf_;
  std::vector<ClientChannel> channels_;
  std::map<std::string, uint64_t> channel_map_;
  linked::List subscriptions_;
};

// Publish a raw struct as a message to the bus.
template <typename T>
int Fusebus::Publish(uint64_t channelno, const Publishable<T>& msg) {
  msg.write_type = WRITE_MESSAGE;
  msg.channelno = channelno;
  return write(bus_fd_, &msg, sizeof(msg));
}

}  // namespace fusebus
