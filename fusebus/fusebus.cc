// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include "fusebus/fusebus.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>

#include <fuse/fuse_lowlevel.h>

namespace fusebus {

MemRing::MemRing(uint64_t data_size)
    : data_size_(data_size),
      data_begin_(nullptr),
      data_end_(nullptr),
      free_begin_(nullptr),
      free_end_(nullptr),
      initialized_(false) {}

int MemRing::Initialize() {
  if (initialized_) {
    return -1;
  }

  // integer divide, round up
  data_size_ = RoundToPage(data_size_, MAX_ALIGN);

  // mmap is going to map us a region that is a multiple of the page size so
  // we may as well use any extra space that we get through this call.
  const int page_size = getpagesize();
  if (page_size > 0) {
    data_size_ = RoundToPage(data_size_, page_size);
  }

  assert(data_size_ > 0);

  data_begin_ =
      reinterpret_cast<uint8_t*>(mmap(/*addr=*/0,
                                      /*length=*/data_size_,
                                      /*prot=*/PROT_READ | PROT_WRITE,
                                      /*flags=*/MAP_PRIVATE | MAP_ANONYMOUS,
                                      /*fd=*/-1,
                                      /*offset=*/0));
  if (!data_begin_) {
    return -1;
  }
  data_end_ = data_begin_ + data_size_;
  free_begin_ = data_begin_;
  free_end_ = data_end_;

  initialized_ = true;
  return 0;
}

// unmap memory and reset pointers
int MemRing::Deinitialize() {
  if (!initialized_) {
    return -1;
  }

  if (data_begin_) {
    munmap(data_begin_, data_size_);
  }

  free_begin_ = free_end_ = data_begin_ = data_end_ = nullptr;
  initialized_ = false;
  return 0;
}

uint64_t MemRing::GetTotalSize() const {
  return (data_end_ - data_begin_);
}

uint64_t MemRing::GetUsedSize() const {
  if (free_begin_ <= free_end_) {
    return GetTotalSize() - GetFreeSize();
  } else {
    return free_begin_ - free_end_;
  }
}

uint64_t MemRing::GetFreeSize() const {
  if (free_begin_ <= free_end_) {
    return free_end_ - free_begin_;
  } else {
    return GetTotalSize() - GetUsedSize();
  }
}

// Request `size` continuous bytes from the currently free segment of the
// ring. If the free segment of the ring wraps around the end of the buffer,
// then the allocation will only go up to the end of the buffer, in which
// case a second allocation must be requested.
//
// If `size` is greater than the amount of freespace in the ring then -1 is
// returned. Otherwise return 0.
int MemRing::RequestAllocation(MemBlock* block, const uint64_t size) {
  if (!initialized_) {
    return -1;
  }
  assert(size > 0);

  const uint64_t stride_requested = RoundToPage(size, MAX_ALIGN);
  if (stride_requested > GetFreeSize()) {
    return -1;
  }

  if (free_begin_ == data_end_) {
    // The free segment pointer currently points to one-past the end of the
    // data buffer. If free_end_ currently points to the same spot there
    // is nothing we can do, however that shouldn't be the case because
    // we already know that GetFreeSize() >= stride_requested > 0.
    assert(free_begin_ != free_end_);
    free_begin_ = data_begin_;
  }

  if (free_begin_ < free_end_) {
    // The free segment is continuous, we can simply allocate right out of it
    block->begin = free_begin_;
    block->end = free_begin_ + stride_requested;
    block->size = size;
    free_begin_ += stride_requested;
    return 0;
  } else {
    // The free segment wraps around. Note that this doesn't necessarily mean
    // that we ran out of space for this request. Rather, it could be that the
    // first allocated message has been freed (due to a need for message
    // objects, rather than memory).
    const uint64_t stride_available = data_end_ - free_begin_;
    const uint64_t stride = std::min(stride_requested, stride_available);
    block->begin = free_begin_;
    block->end = free_begin_ + stride;
    if (stride < size) {
      block->size = stride;
    } else {
      block->size = size;
    }
    free_begin_ = block->end;
    if (free_begin_ >= data_end_) {
      free_begin_ = data_begin_;
    }
    return 0;
  }
  return 0;
}

int MemRing::ReleaseAllocation(MemBlock* block) {
  if (!initialized_) {
    return -1;
  }

  if (free_end_ == data_end_) {
    // If the free store currently ends at the back of the data store, then the
    // block being released must be at the front of the data store.
    if (block->begin != data_begin_) {
      return -3;
    }
  } else {
    // Otherwise the block must be at the end of the current free store.
    if (block->begin != free_end_) {
      return -4;
    }
  }

  free_end_ = block->end;
  // NOTE(josh): If ``free_end_`` points to ``data_end_`` right now we would
  // want to wrap it around to ``data_begin_``. But we can't yet because
  // ``free_begin_`` might be pointing to ``data_begin_`` in which case we
  // would improroperly account the entire store as in-use.

  memset(block, 0, sizeof(MemBlock));
  return 0;
}

Message* Message::Reset() {
  memset(this, 0, sizeof(Message));
  readers.Clear();
  node.MakeRing();
  return this;
}

uint64_t Message::GetStride() {
  return mem[0].GetStride() + mem[1].GetStride();
}

uint64_t Message::WriteData(const uint8_t* src, uint64_t src_size) {
  const uint64_t first_write = std::min(src_size, mem[0].size);
  const uint64_t second_write = std::min(src_size - first_write, mem[1].size);
  if (first_write) {
    memcpy(mem[0].begin, src, first_write);
  }
  if (second_write) {
    memcpy(mem[1].begin, src + first_write, mem[1].size);
  }
  return first_write + second_write;
}

uint64_t Message::ReadMeta(uint8_t* dest, uint64_t dest_size) {
  const uint64_t read_size = std::min(dest_size, sizeof(MessageMeta));
  memcpy(dest, &meta, read_size);
  return read_size;
}

uint64_t Message::ReadData(uint8_t* dest, uint64_t dest_size) {
  const uint64_t first_read = std::min(dest_size, mem[0].size);
  const uint64_t second_read = std::min(dest_size - first_read, mem[1].size);
  if (first_read) {
    memcpy(dest, mem[0].begin, first_read);
  }
  if (second_read) {
    memcpy(dest + first_read, mem[1].begin, second_read);
  }
  return first_read + second_read;
}

MessageStore::MessageStore(
    const uint64_t max_messages)  // NOLINT(runtime/explicit)
    : store_(nullptr),
      max_messages_(max_messages),
      initialized_(false) {}

int MessageStore::Initialize() {
  if (initialized_) {
    return -1;
  }

  // mmap is going to map us a region that is a multiple of the page size so
  // we may as well use any extra space that we get through this call.
  const int page_size = getpagesize();
  if (page_size > 0) {
    max_messages_ = RoundToPage(max_messages_ * sizeof(Message), page_size) /
                    sizeof(Message);
  }
  assert(max_messages_ > 0);

  store_ = reinterpret_cast<Message*>(
      mmap(/*addr=*/0,
           /*length=*/max_messages_ * sizeof(Message),
           /*prot=*/PROT_READ | PROT_WRITE,
           /*flags=*/MAP_PRIVATE | MAP_ANONYMOUS,
           /*fd=*/-1,
           /*offset=*/0));
  if (!store_) {
    return -1;
  }

  for (uint64_t idx = 0; idx < max_messages_; ++idx) {
    free_list_.PushBack(&store_[idx].node);
  }

  initialized_ = true;
  return 0;
}

int MessageStore::Deinitialize() {
  if (!initialized_) {
    return -1;
  }

  if (store_) {
    munmap(store_, max_messages_ * sizeof(Message));
  }

  initialized_ = false;
  return 0;
}

Message* MessageStore::Alloc() {
  linked::Node* node = free_list_.PopFront();
  if (node) {
    return container_of(node, &Message::node)->Reset();
  } else {
    return nullptr;
  }
}

void MessageStore::Free(Message* msg) {
  free_list_.PushBack(&msg->node);
}

uint64_t MessageStore::GetMaxMessages() const {
  return max_messages_;
}

MessageRing::MessageRing(
    const MessageRingConfig& config)  // NOLINT(runtime/explicit)
    : config_(config),
      mem_ring_(config.data_size_req),
      msg_store_(config.max_messages),
      serno_(0),
      initialized_(false) {}

// Map memory, setup pointers, initialize lists
int MessageRing::Initialize() {
  int err = 0;

  if (initialized_) {
    return false;
  }
  serno_ = 0;
  initialized_ = true;

  err = mem_ring_.Initialize();
  if (err) {
    return -1;
  }

  err = msg_store_.Initialize();
  if (err) {
    return -1;
  }

  if (config_.max_msg_size > mem_ring_.GetTotalSize() / 4) {
    config_.max_msg_size = mem_ring_.GetTotalSize() / 4;
  }

  return 0;
}

// Release memory, zero out stuff.
int MessageRing::Deinitialize() {
  if (!initialized_) {
    return -1;
  }
  int err = 0;

  err = mem_ring_.Deinitialize();
  if (err) {
    return -1;
  }

  err = msg_store_.Deinitialize();
  if (err) {
    return -1;
  }

  initialized_ = false;
  return 0;
}

int MessageRing::RegisterClient(Client* client) {
  if (queue_msgs_) {
    client->msg = container_of(queue_msgs_.Front(), &Message::node);
    client->msg->readers.PushBack(&client->node);
  } else {
    client->msg = nullptr;
    finished_readers_.PushBack(&client->node);
  }
  return 0;
}

int MessageRing::UnregisterClient(Client* client) {
  client->node.Remove();
  client->msg = nullptr;
  return 0;
}

// Read as many messages as possible into the clients receive buffer
int MessageRing::Receive(Client* client, uint8_t* dest, uint64_t dest_size) {
  if (dest_size < sizeof(MessageMeta)) {
    return -1;
  }

  uint64_t bytes_left = dest_size;
  int bytes_written = 0;

  // The client points to no message, meaning that it has finished reading
  // all messages and is currently at "end of file".
  if (!client->msg) {
    return 0;
  }

  uint8_t* out = dest;
  linked::Iterator msg_iter{&client->msg->node};
  for (; msg_iter != queue_msgs_.end() && out < dest + dest_size; ++msg_iter) {
    Message* msg = container_of(*msg_iter, &Message::node);
    client->msg = msg;
    if (!client->subscriptions[msg->meta.channel]) {
      continue;
    }

    if (bytes_left >= sizeof(MessageMeta)) {
      const uint64_t write_len = msg->ReadMeta(out, bytes_left);
      bytes_written += write_len;
      bytes_left -= write_len;
      out += write_len;
    } else {
      break;
    }

    const uint64_t stride = msg->GetStride();
    if (bytes_left >= stride) {
      msg->ReadData(out, bytes_left);
      bytes_written += stride;
      bytes_left -= stride;
      out += stride;
    } else if (bytes_left >= msg->meta.size) {
      const uint64_t write_len = msg->ReadData(out, bytes_left);
      bytes_written += write_len;
      bytes_left -= write_len;
      out += stride;
    } else {
      break;
    }
  }

  for (; msg_iter != queue_msgs_.end(); ++msg_iter) {
    Message* msg = container_of(*msg_iter, &Message::node);
    if (client->subscriptions[msg->meta.channel]) {
      break;
    }
  }

  client->node.Remove();
  if (msg_iter == queue_msgs_.end()) {
    client->msg = nullptr;
    finished_readers_.PushBack(&client->node);
  } else {
    client->msg->readers.PushBack(&client->node);
  }

  return bytes_written;
}

// Read as many messages as possible into the clients receive buffer
int MessageRing::Receive(Client* client, uint64_t dest_size, struct iovec* iov,
                         int* iov_count) {
  if (dest_size < sizeof(MessageMeta)) {
    return -1;
  }

  uint64_t bytes_left = dest_size;
  int bytes_written = 0;

  // The client points to no message, meaning that it has finished reading
  // all messages and is currently at "end of file".
  if (!client->msg) {
    return 0;
  }

  const int num_iov = *iov_count;
  *iov_count = 0;

  linked::Iterator msg_iter{&client->msg->node};
  for (; msg_iter != queue_msgs_.end() && bytes_written < dest_size &&
         *iov_count < num_iov;
       ++msg_iter) {
    Message* msg = container_of(*msg_iter, &Message::node);
    client->msg = msg;
    if (!client->subscriptions[msg->meta.channel]) {
      continue;
    }

    if (bytes_left < sizeof(MessageMeta)) {
      break;
    }

    iov[(*iov_count)++] = {&msg->meta, sizeof(MessageMeta)};
    bytes_written += sizeof(MessageMeta);
    bytes_left -= sizeof(MessageMeta);

    // TODO(josh): we only need one iov if only one mem slot is full
    if (num_iov - *iov_count < 2) {
      break;
    }

    // TODO(josh): allow copying less than a full stride as long as the data
    // fits
    if (bytes_left < msg->GetStride()) {
      break;
    }

    if (msg->mem[0].size) {
      iov[(*iov_count)++] = {msg->mem[0].begin, msg->mem[0].GetStride()};
      bytes_written += msg->mem[0].GetStride();
      bytes_left -= msg->mem[0].GetStride();
    }
    // if (*iov_count >= num_iov) {
    //   break;
    // }

    if (msg->mem[1].size) {
      iov[(*iov_count)++] = {msg->mem[1].begin, msg->mem[1].GetStride()};
      bytes_written += msg->mem[1].GetStride();
      bytes_left -= msg->mem[1].GetStride();
    }
    // if (*iov_count >= num_iov) {
    //   break;
    // }
  }

  // TODO(josh): uncomment the above to break cases and refactor the logic to
  // deal with the case that we have enough write buffer to write the message
  // but not enough to write the stride... in which case we need to make sure
  // we advance past that message and fast-forward past any non-subscribed
  // messages.
  for (; msg_iter != queue_msgs_.end(); ++msg_iter) {
    Message* msg = container_of(*msg_iter, &Message::node);
    if (client->subscriptions[msg->meta.channel]) {
      break;
    }
  }

  client->node.Remove();
  if (msg_iter == queue_msgs_.end()) {
    client->msg = nullptr;
    finished_readers_.PushBack(&client->node);
  } else {
    client->msg->readers.PushBack(&client->node);
  }

  return bytes_written;
}

// Read a message from the clients publish buffer into the message ring
int MessageRing::Publish(Client* client, const uint8_t* src,
                         uint64_t src_size) {
  assert(initialized_);
  assert(config_.max_msg_size <= mem_ring_.GetTotalSize() / 4);
  if (src_size > config_.max_msg_size) {
    return -1;
  }

  if (src_size < sizeof(uint64_t)) {
    return -2;
  }

  const uint8_t* body_src = src + sizeof(uint64_t);
  const uint64_t body_size = src_size - sizeof(uint64_t);
  if (!body_size) {
    // Message body is empty
    return 0;
  }

  linked::List orphaned_readers;
  Message* new_msg = msg_store_.Alloc();
  if (!new_msg) {
    // Max number of messages has been exceeded, we need to drop one just to
    // get an available slot.
    // TODO(josh): de-duplicate with below:
    assert(queue_msgs_);
    linked::Node* pop_node = queue_msgs_.PopFront();
    Message* pop_msg = container_of(pop_node, &Message::node);
    orphaned_readers.StealNodesFrom(&pop_msg->readers);
    if (pop_msg->mem[0].size) {
      int err = mem_ring_.ReleaseAllocation(&pop_msg->mem[0]);
      assert(!err);
    }
    if (pop_msg->mem[1].size) {
      int err = mem_ring_.ReleaseAllocation(&pop_msg->mem[1]);
      assert(!err);
    }
    msg_store_.Free(pop_msg);
    new_msg = msg_store_.Alloc();
  }

  assert(new_msg);
  new_msg->meta.channel = *reinterpret_cast<const uint64_t*>(src);
  new_msg->meta.serno = serno_++;
  new_msg->meta.client = client->id;
  new_msg->meta.size = body_size;

  // Drop messages from the front of the queue until there's enough space
  // in the memring to hold the new message.
  while (mem_ring_.GetFreeSize() < body_size) {
    assert(queue_msgs_);
    linked::Node* pop_node = queue_msgs_.PopFront();
    Message* pop_msg = container_of(pop_node, &Message::node);
    orphaned_readers.StealNodesFrom(&pop_msg->readers);
    if (pop_msg->mem[0].size) {
      int err = mem_ring_.ReleaseAllocation(&pop_msg->mem[0]);
      assert(!err);
    }
    if (pop_msg->mem[1].size) {
      int err = mem_ring_.ReleaseAllocation(&pop_msg->mem[1]);
      assert(!err);
    }
    msg_store_.Free(pop_msg);
  }

  int err = mem_ring_.RequestAllocation(&new_msg->mem[0], body_size);
  assert(!err);
  if (new_msg->mem[0].size < body_size) {
    const uint64_t more_space_needed = body_size - new_msg->mem[0].size;
    err = mem_ring_.RequestAllocation(&new_msg->mem[1], more_space_needed);
    assert(!err);
  }
  new_msg->meta.stride = new_msg->GetStride();

  const uint64_t bytes_written = new_msg->WriteData(body_src, body_size);
  assert(bytes_written == body_size);
  queue_msgs_.PushBack(&new_msg->node);

  // Take all clients who were pointed at one of the messages we removed, and
  // point them instead at the new head of the queue. Each of these clients
  // is dropping a message.
  // TODO(josh): accumulate a count of message drops in each client for stats
  // tracking purposes.
  Message* queue_msgs_front = container_of(queue_msgs_.Front(), &Message::node);
  for (linked::Node* node : orphaned_readers) {
    container_of(node, &Client::node)->msg = queue_msgs_front;
  }
  queue_msgs_front->readers.StealNodesFrom(&orphaned_readers);

  // Take all clients who had finished reading the entire queue up to this
  // point in time, and point them at the new message so that their next
  // Recv() request reads off this new message.
  for (linked::Iterator iter = finished_readers_.begin();
       iter != finished_readers_.end();) {
    Client* client = container_of(*iter, &Client::node);

    // NOTE(josh): if we Remove() the node the iterator becomes invalid, so
    // we need to advance it here first.
    ++iter;

    if (client->subscriptions[new_msg->meta.channel]) {
      client->node.Remove();
      client->msg = new_msg;
      client->msg->readers.PushBack(&client->node);
      if (client->poll_handle) {
        fuse_lowlevel_notify_poll(client->poll_handle.get());
        client->poll_handle.reset();
      }
    }
  }

  return 0;
}

// TODO(josh): Client::Init(), Client::Client()
void InitClient(Client* client) {
  // client->id = id;
  client->msg = nullptr;
  client->subscriptions.Clear();
  client->node.MakeRing();
  client->poll_handle.reset();
}

ChannelRegistry::ChannelRegistry(uint64_t max_channels)
    : max_channels_(max_channels), initialized_(false) {}

int ChannelRegistry::Initialize() {
  if (initialized_) {
    return -1;
  }
  initialized_ = true;

  int pagesize = getpagesize();
  if (pagesize < 1) {
    pagesize = 4096;
  }

  const uint64_t file_size =
      RoundToPage(max_channels_ * MAX_CHANNEL_LEN, pagesize);
  file_begin_ =
      reinterpret_cast<char*>(mmap(/*addr=*/0,
                                   /*length=*/file_size,
                                   /*prot=*/PROT_READ | PROT_WRITE,
                                   /*flags=*/MAP_PRIVATE | MAP_ANONYMOUS,
                                   /*fd=*/-1,
                                   /*offset=*/0));
  file_ptr_ = file_begin_;
  file_end_ = file_begin_ + file_size;
  if (!file_begin_) {
    return -1;
  }

  channels_.reserve(max_channels_);
  return 0;
}

int ChannelRegistry::Deinitialize() {
  if (!initialized_) {
    return -1;
  }
  initialized_ = false;
  if (file_begin_) {
    munmap(file_begin_, max_channels_ * MAX_CHANNEL_LEN);
  }
  channels_.clear();
  return 0;
}

int ChannelRegistry::GetId(const std::string& channel, uint64_t* id,
                           bool* is_new) {
  auto iter = index_.find(channel);
  if (iter == index_.end()) {
    if (1 + channel.size() > (file_end_ - file_ptr_)) {
      return -1;
    }

    if (channel.size() > MAX_CHANNEL_LEN) {
      return -2;
    }

    if (!id) {
      return -3;
    }

    const uint64_t new_id = channels_.size();
    if (new_id >= max_channels_) {
      return -4;
    }

    if (id) {
      *id = new_id;
    }
    if (is_new) {
      *is_new = true;
    }

    channels_.push_back(channel);
    index_[channel] = new_id;

    strncpy(file_ptr_, &channel[0], channel.size());
    file_ptr_ += channel.size();
    *(file_ptr_++) = '\n';
  } else {
    if (id) {
      *id = iter->second;
    }
    if (is_new) {
      *is_new = false;
    }
  }
  return 0;
}

int ChannelRegistry::GetName(const uint64_t id, std::string* out) {
  if (id < channels_.size()) {
    *out = channels_[id];
    return 0;
  } else {
    return -1;
  }
}

int Subscription::MatchChannels(const std::vector<std::string>& channels,
                                Client* client) {
  for (uint64_t chanid = 0; chanid < channels.size(); ++chanid) {
    if (re2::RE2::FullMatch(channels[chanid], *pattern)) {
      client->subscriptions[chanid] = true;
    }
  }
  return 0;
}

SubscriptionManager::SubscriptionManager(uint64_t max_subscriptions)
    : initialized_(false), max_subscriptions_(max_subscriptions) {}

int SubscriptionManager::Initialize() {
  if (initialized_) {
    return -1;
  }
  subscription_obs_.resize(max_subscriptions_);
  for (uint64_t idx = 0; idx < max_subscriptions_; ++idx) {
    subscription_obs_[idx].id = idx;
    inactive_subscriptions_.PushBack(&subscription_obs_[idx].node);
  }
  initialized_ = true;
  return 0;
}

int SubscriptionManager::Deinitialize() {
  if (!initialized_) {
    return -1;
  }
  subscription_obs_.clear();
  inactive_subscriptions_.Clear();
  active_subscriptions_.Clear();
  initialized_ = false;
  return 0;
}

int SubscriptionManager::ReleaseSubscriptions(Client* client) {
  while (client->subs) {
    Subscription* sub =
        container_of(client->subs.Front(), &Subscription::client_node);
    if (ReleaseSubscription(sub)) {
      return -1;
    } else {
      client->subs.PopFront();
    }
  }
  inactive_subscriptions_.StealNodesFrom(&client->subs);
  return 0;
}

Subscription* SubscriptionManager::AddSubscription(Client* client,
                                                   const std::string& channel) {
  if (inactive_subscriptions_) {
    Subscription* sub =
        container_of(inactive_subscriptions_.PopFront(), &Subscription::node);

    sub->pattern.reset(new re2::RE2(channel));
    sub->client = client;
    active_subscriptions_.PushBack(&sub->node);
    sub->client->subs.PushBack(&sub->client_node);
    return sub;
  } else {
    return nullptr;
  }
}

int SubscriptionManager::ReleaseSubscription(Subscription* sub) {
  if (sub < &subscription_obs_[0] ||
      sub >= &subscription_obs_[0] + max_subscriptions_) {
    return -1;
  }

  sub->node.Remove();
  sub->client_node.Remove();
  sub->client = nullptr;
  inactive_subscriptions_.PushBack(&sub->node);
  return 0;
}

int SubscriptionManager::NotifyNewChannel(const uint64_t channel_id,
                                          const std::string& channel) {
  if (channel_id >= MAX_CHANNELS) {
    return -1;
  }

  for (linked::Node* node : active_subscriptions_) {
    Subscription* sub = container_of(node, &Subscription::node);
    if (re2::RE2::FullMatch(channel, *(sub->pattern))) {
      sub->client->subscriptions[channel_id] = true;
    }
  }
  return 0;
}

int SubscriptionManager::ExchangeToken(const uint64_t id, Subscription** sub) {
  if (id >= max_subscriptions_) {
    return -1;
  }

  *sub = &subscription_obs_[id];
  if ((*sub)->client) {
    return 0;
  } else {
    return -1;
  }
}

ClientManager::ClientManager(uint64_t max_clients)
    : initialized_(false), max_clients_(max_clients) {}

int ClientManager::Initialize() {
  if (initialized_) {
    return -1;
  }
  client_obs_.resize(max_clients_);
  for (uint64_t idx = 0; idx < max_clients_; ++idx) {
    client_obs_[idx].id = idx;
    free_clients_.PushBack(&client_obs_[idx].node);
  }

  initialized_ = true;
  return 0;
}

int ClientManager::Deinitialize() {
  if (!initialized_) {
    return -1;
  }
  client_obs_.clear();
  free_clients_.Clear();
  initialized_ = false;
  return 0;
}

// return an unused client object or nullptr if they are all in use.
Client* ClientManager::CreateClient() {
  if (free_clients_) {
    Client* client = container_of(free_clients_.PopFront(), &Client::node);
    InitClient(client);
    return client;
  } else {
    return nullptr;
  }
}

int ClientManager::FreeClient(Client* client) {
  if (client < &client_obs_[0] || client >= &client_obs_[0] + max_clients_) {
    return -1;
  }

  assert(!client->subs);
  if (client->subs) {
    return -1;
  }

  client->poll_handle.reset();
  client->node.Remove();
  client->msg = nullptr;
  free_clients_.PushBack(&client->node);
  return 0;
}

int ClientManager::ExchangeToken(const uint64_t id, Client** client) {
  if (id >= max_clients_) {
    dprintf(STDERR_FILENO, "client index %lu >= max clients %lu\n", id,
            max_clients_);
    return -1;
  }

  *client = &client_obs_[id];
  return 0;
}

}  // namespace fusebus
