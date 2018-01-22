// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "fusebus/fusebus_client.h"

namespace fusebus {

Fusebus::Fusebus(const std::string& busdir) : bus_fd_(0), busdir_(busdir) {
  const std::string busfile_path = Join("/", busdir, "fusebus");
  bus_fd_ = open(busfile_path.c_str(), O_RDWR);
  assert(bus_fd_ != -1);

  const std::string chanfile_path = Join("/", busdir, "channels");
  chan_fd_ = open(chanfile_path.c_str(), O_RDWR);
  assert(chan_fd_ != -1);

  buf_.resize(MAX_MSG_SIZE);
  channels_.reserve(MAX_CHANNELS);
}

Fusebus::~Fusebus() {
  while (subscriptions_) {
    ClientSubscription* subs =
        container_of(subscriptions_.PopFront(), &ClientSubscription::node);
    Unsubscribe(subs);
  }
  if (bus_fd_ > 0) {
    close(bus_fd_);
  }
  if (chan_fd_ > 0) {
    close(chan_fd_);
  }
}

int Fusebus::WriteChannel(const std::string& channel) {
  std::ofstream outfile(Join("/", busdir_, "channels"));
  if (!outfile.good()) {
    return -1;
  }
  outfile << channel;
  outfile.close();
  return 0;
}

int Fusebus::ReadChannels() {
  std::ifstream infile(Join("/", busdir_, "channels"));
  if (!infile.good()) {
    return -1;
  }
  std::string channel;
  for (uint64_t idx = 0; std::getline(infile, channel); idx++) {
    if (idx < channels_.size()) {
      continue;
    }
    channels_.emplace_back();
    channels_[idx].id = idx;
    channels_[idx].name = channel;
    for (linked::Node* node : subscriptions_) {
      ClientSubscription* sub = container_of(node, &ClientSubscription::node);
      if (re2::RE2::FullMatch(channel, *(sub->channel_pattern))) {
        ChannelLink* link = new ChannelLink{&channels_[idx], sub};
        sub->links.PushBack(&link->subs_node);
        channels_[idx].links.PushBack(&link->chan_node);
      }
    }
    channel_map_[channel] = idx;
  }
  infile.close();
  return 0;
}

int Fusebus::GetChannel(const std::string& channel, uint64_t* id) {
  auto iter = channel_map_.find(channel);
  if (iter == channel_map_.end()) {
    int err = WriteChannel(channel);
    if (err) {
      return err;
    }
    err = ReadChannels();
    if (err) {
      return err;
    }
  }
  iter = channel_map_.find(channel);
  assert(iter != channel_map_.end());
  *id = iter->second;
  return 0;
}

int Fusebus::Subscribe(const std::string& channel_pattern,
                       const RawCallback& callback, ClientSubscription** out) {
  uint64_t write_type = WRITE_SUBSCRIBE;
  iovec iov[] = {
      {&write_type, sizeof(uint64_t)},
      {const_cast<char*>(&channel_pattern[0]), channel_pattern.size()},
  };
  int subid = writev(bus_fd_, iov, 2);
  if (subid < 0) {
    return subid;
  }

  ClientSubscription* sub = new ClientSubscription{
      subid, std::unique_ptr<re2::RE2>(new re2::RE2(channel_pattern)),
      callback};

  for (ClientChannel& channel : channels_) {
    if (re2::RE2::FullMatch(channel.name, *(sub->channel_pattern))) {
      ChannelLink* link = new ChannelLink{&channel, sub};
      sub->links.PushBack(&link->subs_node);
      channel.links.PushBack(&link->chan_node);
    }
  }

  if (out) {
    *out = sub;
  }
  return 0;
}

int Fusebus::Unsubscribe(ClientSubscription* subs) {
  // unsubscribe from the server
  uint64_t write_type = WRITE_UNSUBSCRIBE;
  iovec iov[] = {{&write_type, sizeof(uint64_t)},
                 {&subs->id, sizeof(uint64_t)}};
  int err = writev(bus_fd_, iov, 2);

  // remove all channel links
  while (subs->links) {
    ChannelLink* link =
        container_of(subs->links.Front(), &ChannelLink::subs_node);
    link->subs_node.Remove();
    link->chan_node.Remove();
    delete link;
  }

  // remove from the list of subscriptions
  subs->node.Remove();

  delete subs;
  return err;
}

int Fusebus::Publish(const uint64_t channelno, uint8_t* buf, uint64_t bufsize) {
  uint8_t header[2 * sizeof(uint64_t)];
  *reinterpret_cast<uint64_t*>(header) = WRITE_MESSAGE;
  *reinterpret_cast<uint64_t*>(header + sizeof(uint64_t)) = channelno;

  iovec iov[2] = {{header, 2 * sizeof(uint64_t)}, {buf, bufsize}};

  return writev(bus_fd_, iov, 2);
}

int Fusebus::PublishPad(const uint64_t channelno, uint8_t* buf,
                        uint64_t bufsize) {
  *reinterpret_cast<uint64_t*>(buf) = WRITE_MESSAGE;
  *reinterpret_cast<uint64_t*>(buf + sizeof(uint64_t)) = channelno;

  return write(bus_fd_, buf, bufsize);
}

int Fusebus::GetFileno() {
  return bus_fd_;
}

int Fusebus::Handle() {
  int read_size = read(bus_fd_, &buf_[0], buf_.size());
  if (read_size < 0) {
    return read_size;
  }

  if (read_size < sizeof(RecvMessage)) {
    return 0;
  }

  for (uint64_t offset = 0; offset < read_size;) {
    const RecvMessage* message = reinterpret_cast<RecvMessage*>(&buf_[offset]);
    offset += sizeof(MessageMeta) + message->meta.stride;
    if (message->meta.channel > channels_.size()) {
      ReadChannels();
    }
    if (message->meta.channel > channels_.size()) {
      continue;
    }

    ClientChannel& channel = channels_[message->meta.channel];
    for (linked::Node* node : channel.links) {
      ChannelLink* link = container_of(node, &ChannelLink::chan_node);
      ClientSubscription* subs = link->subs;
      subs->callback(*message);
    }
  }
  return 0;
}

}  // namespace fusebus
