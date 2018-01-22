#pragma once
// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com

#include <sys/uio.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#define FUSE_USE_VERSION 29
#include <fuse/fuse_lowlevel.h>
#include <re2/re2.h>

namespace fusebus {

// NOTE(josh):
// Linux Version
// #define container_of(ptr, type, member)               \
//   ({                                                  \
//     const typeof(((type*)0)->member)* __mptr = (ptr); \
//     (type*)((char*)__mptr - offsetof(type, member));  \
//   })

// NOTE(josh): borrowed from
// http://shimpossible.blogspot.com/2013/08/containerof-and-offsetof-in-c.html
template <class P, class M>
size_t offset_of(const M P::*member) {
  return (size_t) & (reinterpret_cast<P*>(0)->*member);
}

// NOTE(josh): borrowed from
// http://shimpossible.blogspot.com/2013/08/containerof-and-offsetof-in-c.html
template <class P, class M>
P* container_of(M* ptr, const M P::*member) {
  return (P*)((char*)ptr - offset_of(member));  // NOLINT(readability/casting)
}

namespace linked {

// Simple linked list node
struct Node {
  Node* prev;
  Node* next;

  void Remove() {
    if (prev) {
      prev->next = next;
    }
    if (next) {
      next->prev = prev;
    }
    prev = nullptr;
    next = nullptr;
  }

  void InsertBefore(Node* other) {
    Remove();
    this->next = other;
    this->prev = other->prev;
    other->prev = this;
    if (this->prev) {
      this->prev->next = this;
    }
  }

  void InsertAfter(Node* other) {
    Remove();
    this->prev = other;
    this->next = other->next;
    other->next = this;
    if (this->next) {
      this->next->prev = this;
    }
  }

  // Turn this node into a single-element ring. Note that this does not
  // Remove() the node if it is currently part of a list and will leave
  // dangling references to this node if called while it is part of an
  // existing list.
  Node* MakeRing() {
    prev = this;
    next = this;
    return this;
  }
};

// Simple iterator over nodes
// TODO(josh): add const iterator
struct Iterator {
  bool operator==(const Iterator& other) const {
    return this->node == other.node;
  }

  bool operator!=(const Iterator& other) const {
    return this->node != other.node;
  }

  Iterator& operator++() {
    if (node) {
      node = node->next;
    }
    return *this;
  }

  Iterator operator++() const {
    if (node) {
      return Iterator{node->next};
    } else {
      return Iterator{node};
    }
  }

  Iterator operator+(size_t off) {
    Iterator other{node};
    for (size_t idx = 0; idx < off; ++idx) {
      ++other;
    }
    return other;
  }

  Iterator& operator--() {
    if (node) {
      node = node->prev;
    }
    return *this;
  }

  Iterator operator--() const {
    if (node) {
      return Iterator{node->prev};
    } else {
      return Iterator{node};
    }
  }

  Iterator operator-(size_t off) {
    Iterator other{node};
    for (size_t idx = 0; idx < off; ++idx) {
      --other;
    }
    return other;
  }

  Node* operator->() {
    return node;
  }

  Node* operator*() {
    return node;
  }

  Node* node;
};

// Simple linked-list. Is actually a doubly-linked ring but one node in
// the ring is special, and stored within this object. That special node
// is the sentinel for start/end of the list
struct List {
 public:
  List() {
    sentinel_.MakeRing();
  }

  Iterator begin() {
    return Iterator{sentinel_.next};
  }

  Iterator rbegin() {
    return Iterator{sentinel_.prev};
  }

  Iterator end() {
    return Iterator{&sentinel_};
  }

  void PushBack(Node* node) {
    node->InsertBefore(&sentinel_);
  }

  void PushFront(Node* node) {
    node->InsertAfter(&sentinel_);
  }

  Node* PopFront() {
    Node* out = sentinel_.next;
    if (out == &sentinel_) {
      return nullptr;
    } else {
      out->Remove();
      return out;
    }
  }

  Node* PopBack() {
    Node* out = sentinel_.prev;
    if (out == &sentinel_) {
      return nullptr;
    } else {
      out->Remove();
      return out;
    }
  }

  Node* Front() {
    if (sentinel_.next == &sentinel_) {
      return nullptr;
    } else {
      return sentinel_.next;
    }
  }

  Node* Back() {
    if (sentinel_.prev == &sentinel_) {
      return nullptr;
    } else {
      return sentinel_.prev;
    }
  }

  bool IsEmpty() const {
    return (sentinel_.next == &sentinel_);
  }

  operator bool() const {
    return !IsEmpty();
  }

  void Clear() {
    sentinel_.MakeRing();
  }

  // Move all nodes from other into this one
  void StealNodesFrom(List* other) {
    if (other->IsEmpty()) {
      return;
    }

    Node* my_head = &sentinel_;
    Node* my_tail = sentinel_.prev;
    Node* splice_begin = other->sentinel_.next;
    Node* splice_end = other->sentinel_.prev;

    assert(my_tail);
    assert(splice_begin);
    assert(splice_end);

    my_tail->next = splice_begin;
    splice_begin->prev = my_tail;
    splice_end->next = my_head;
    my_head->prev = splice_end;
    other->Clear();
  }

  void GiveNodesTo(List* other) {
    other->StealNodesFrom(this);
  }

 protected:
  Node sentinel_;
};

// Simple ring of nodes
struct Ring : List {
 public:
  Ring() {}

  void Push(Node* node) {
    this->PushBack(node);
  }

  Node* Peek() {
    return this->Front();
  }

  Node* Pop() {
    return this->PopFront();
  }
};

}  // namespace linked

// Reference to one bit within a bitset
template <typename T>
struct BitRef {
  T* byte_block_;
  int bit_no_;

  operator bool() const {
    return (*byte_block_) & (0x01 << bit_no_);
  }

  BitRef& operator=(int value) {
    if (value) {
      (*byte_block_) |= (0x01 << bit_no_);
    } else {
      (*byte_block_) &= (~T(0) - (0x01 << bit_no_));
    }
    return *this;
  }
};

// Like std::bitset but returns assignable references
template <uint16_t N, typename T = uint8_t>
struct BitSet {
  enum {
    STORE_ELEMS_ = ((N + 8 * sizeof(T) - 1) / (8 * sizeof(T))),
    STORE_BITS_ = STORE_ELEMS_ * (sizeof(T) * 8),
    EXTRA_BITS_ = (STORE_BITS_ - N),

    // NOTE(josh): we store the data in the least significant bits
    LAST_ELEM_MASK_ = (~T(0) >> EXTRA_BITS_)
  };

  T data[STORE_ELEMS_];

  bool StaticCheck() {
    static_assert(N > 0, "BitSet<0> is not supported");
    static_assert(std::is_unsigned<T>::value,
                  "BitSet block type must be unsigned");
  }

  void Clear() {
    for (size_t idx = 0; idx < STORE_ELEMS_; idx++) {
      data[idx] = 0;
    }
  }

  bool Any() {
    for (size_t idx = 0; idx < STORE_ELEMS_ - 1; idx++) {
      if (data[idx]) {
        return true;
      }
    }
    if (data[STORE_ELEMS_ - 1] & LAST_ELEM_MASK_) {
      return true;
    }
    return false;
  }

  bool All() {
    for (size_t idx = 0; idx < STORE_ELEMS_ - 1; idx++) {
      if (~data[idx]) {
        return false;
      }
    }
    if (~data[STORE_ELEMS_ - 1] & LAST_ELEM_MASK_) {
      return false;
    }
    return true;
  }

  bool None() {
    return !Any();
  }

  BitRef<T> operator[](int idx) {
    const int elemno = idx / (8 * sizeof(T));
    const int bitno = idx % (8 * sizeof(T));
    assert(elemno < STORE_ELEMS_);
    return BitRef<T>{&data[elemno], bitno};
  }

  const BitRef<T> operator[](int idx) const {
    const int elemno = idx / (8 * sizeof(T));
    const int bitno = idx % (8 * sizeof(T));
    assert(elemno < STORE_ELEMS_);
    return BitRef<T>{&data[elemno], bitno};
  }
};

// Integer divison round-up instead of truncate. e.g. 3/2 = 2. Useful for
// paging calculates: i.e. "how many pages of size N do I need to hold n
// data" (answer is n/N round-up).
template <typename T>
inline T DivideRoundUp(T numerator, T denominator) {
  return (numerator + denominator - 1) / denominator;
}

// Return the first multiple of page_size not less than num_bytes.
inline uint64_t RoundToPage(uint64_t num_bytes, uint64_t page_size) {
  const uint64_t num_pages = DivideRoundUp(num_bytes, page_size);
  return num_pages * page_size;
}

// NOTE(josh): I guess this is good enough, used to set the alignment of the
// storage for message objects.
// http://www.gotw.ca/gotw/028.htm
union MaxAlign {
  short dummy0;  // NOLINT(runtime/int)
  long dummy1;   // NOLINT(runtime/int)
  double dummy2;
  long double dummy3;
  void* dummy4;
  /*...and pointers to functions, pointers to
       member functions, pointers to member data,
       pointers to classes, eye of newt, ...*/
};

enum Constants {
  // Messages in the storage block will be aligned to this
  MAX_ALIGN = sizeof(MaxAlign),
  // Maximum number of channels supported by the library. This should be
  // a power of 2 > 8.
  MAX_CHANNELS = 1024,
  // Maximum length of any one channel string, including null terminator
  MAX_CHANNEL_LEN = 512,

  // TODO(josh): remove from here below,  the rest of these are really runtime
  // params.

  // Maximum number of clients allowed,
  MAX_CLIENTS = 1024,
  // Maximum total number of subscriptions allowed
  MAX_SUBSCRIPTIONS = 10240,
  // Maximum total storage for messages,
  MAX_MESSAGE_STORE = 4 * 1024 * 1024,
  // Maximum number of messages,
  MAX_MESSAGES = 4 * 1024,
  // Maximum size of any one message
  MAX_MSG_SIZE = 1024,
};

// The first 8-bytes of a write to the server indicates what the write is
// intended for.
enum WriteType {
  WRITE_SUBSCRIBE = 0,  //
  WRITE_UNSUBSCRIBE,    //
  WRITE_MESSAGE
};

// Represents an allocated region of memory out of a continuous pool
struct MemBlock {
  uint8_t* begin;  //< first byte in the allocation
  uint8_t* end;    //< first byte past the end of the allocation
  uint64_t size;   //< size in bytes of actual data. Anything past (begin+size)
                   //  and up to (but excluding) end is alignment padding

  uint64_t GetStride() {
    assert(end >= begin);
    return end - begin;
  }
};

// Manages a region of continous memory as ring. Blocks of memory are
// allocatable out of the back of the ring, and must be freed into the front
// of the ring. In other words the set of outstanding allocated buffers must
// be continuous, modulo the end of the buffer.
class MemRing {
 public:
  MemRing(uint64_t data_size);  // NOLINT(runtime/explicit)

  // map memory and initialize pointers
  int Initialize();

  // unmap memory and reset pointers
  int Deinitialize();

  // return the total number of bytes mapped for storage
  uint64_t GetTotalSize() const;

  // return the number of bytes currently allocated (in use)
  uint64_t GetUsedSize() const;

  // return the number of bytes available for new allocations (free)
  uint64_t GetFreeSize() const;

  // Request `size` continuous bytes from the currently free segment of the
  // ring. If the free segment of the ring wraps around the end of the buffer,
  // then the allocation will only go up to the end of the buffer, in which
  // case a second allocation must be requested.
  //
  // If `size` is greater than the amount of freespace in the ring then -1 is
  // returned. Otherwise return 0.
  int RequestAllocation(MemBlock* block, const uint64_t size);

  // Return an allocation back to the pool.
  int ReleaseAllocation(MemBlock* block);

 private:
  uint64_t data_size_;   //< size of the memory pool
  uint8_t* data_begin_;  //< first byte of the mapped memory range
  uint8_t* data_end_;    //< first byte passed the end of the mapped memory
  uint8_t* free_begin_;  //< the first byte available for allocation
  uint8_t* free_end_;    //< the first byte past the end of the region of
                         //  bytes available for allocation. This may be
                         //  numerically smaller than free_begin_ due to
                         //  wrap around.
  bool initialized_;
};

// Header prepended before data content in buffer returned to client during
// a read() operation.
struct MessageMeta {
  uint64_t channel;  //< channel number the message was published on
  uint64_t serno;    //< global serial number of the message
  uint64_t client;   //< client id of the publisher of this message
  uint64_t size;     //< number of bytes following this header that contain
                     //  message data
  uint64_t stride;   //< number of bytes following this header before the start
                     //  of the next header
};

struct Message {
  MessageMeta meta;  //< metadata header for client read/write buffers
  MemBlock mem[2];   //< Allocation of where the actual message data is stored,
                     //  only one of these is full unless the message
                     //  was split across the storage boundaries.
  linked::List readers;  //< a list of readers for whom this is the first
                         //  unreceived message
  linked::Node node;     //< the position of this message in the queue or in the
                         //  free list

  // clear data and reset the reader list
  Message* Reset();

  // return the total continuous region of memory required to store all
  // allocated memory blocks (there are either 0, 1, or 2 blocks).
  uint64_t GetStride();

  // copy data from `src` into the allocated memory block(s). Generally it will
  // be copied into a single continuous region but it may be split across two.
  // Returns the number of bytes copied, which may be less than `src_size` if
  // the allocation is smaller than `src_size`.
  uint64_t WriteData(const uint8_t* src, uint64_t src_size);

  // copy up to `dest_size` bytes of the  MessageMeta data structure into
  // `dest`. returns the number of bytes copied.
  uint64_t ReadMeta(uint8_t* dest, uint64_t dest_size);

  // copy up to `dest_size` bytes of message data out of allocated storage
  // into `dest`. returns the number of bytes copied.
  uint64_t ReadData(uint8_t* dest, uint64_t dest_size);
};

// Maintains a pre-allocated pool of message objects and provies methods to
// allocate objects out of the pool or free them back to the pool.
class MessageStore {
 public:
  MessageStore(const uint64_t max_messages);  // NOLINT(runtime/explicit)

  // map memory and initialize pointers
  int Initialize();

  // unmap memory and clear pointers
  int Deinitialize();

  // return an unused message object out of the pool
  Message* Alloc();

  // release a message object back to the pool
  void Free(Message* msg);

  // return the configured maximum number of messages available
  uint64_t GetMaxMessages() const;

 private:
  Message* store_;
  uint64_t max_messages_;
  linked::List free_list_;
  bool initialized_;
};

struct PollPtrDeleter {
  void operator()(fuse_pollhandle* ph) {
    fuse_pollhandle_destroy(ph);
  }
};

typedef std::unique_ptr<fuse_pollhandle, PollPtrDeleter> PollPtr;

// State maintained for each "client" connected to the bus. This structure
// allows us to efficiently find the next message a client needs to read,
// as well as to correlate publishes and receives.
struct Client {
  uint64_t id;   //< serial number for this client
  Message* msg;  //< the first message that this client has not yet
                 //  read

  // Bit `i` is set if client is subscribed to channel `i`
  BitSet<MAX_CHANNELS, uint64_t> subscriptions;

  linked::Node node;  //< presence in the reader list for a message node,
                      //  or in the free list if unallocated
  linked::List subs;  //< list of subscriptions, to be matched against any
                      //  new channels that are encountered

  // If an outstanding poll requests exists for this client, then this is the
  // pointer to that request.
  PollPtr poll_handle;
};

// TODO(josh): Client::Init(), Client::Client()
void InitClient(Client* client);

// Configuration options for the message bus
struct MessageRingConfig {
  uint64_t data_size_req;  //< requested number of bytes to reserve for data
                           //  storage, actual storage may be larger
  uint64_t max_messages;   //< maximum number of messages stored in the bus
  uint64_t max_msg_size;   //< largest size of a single message published to
                           //  the bus.
  uint64_t max_clients;    //< maximum number of clients allowed
};

// Manage a queue of arbitrary message data within a continuous block of
// memory. Message data is stored in backing memory as a ring of allocations.
// New messages push old messages out of the queue. Efficiently maintains
// client pointers to the next message that they'll need to read.
class MessageRing {
 public:
  MessageRing(const MessageRingConfig& config);  // NOLINT(runtime/explicit)

  // Map memory, setup pointers, initialize lists
  int Initialize();

  // Release memory, zero out stuff.
  int Deinitialize();

  // Inform the bus about a client which wishes to partipate
  int RegisterClient(Client* client);

  // Tell the bus to forget about a participating client
  int UnregisterClient(Client* client);

  // Read as many messages as possible into the clients receive buffer
  int Receive(Client* client, uint8_t* dest, uint64_t dest_size);
  int Receive(Client* client, uint64_t dest_size, struct iovec* iov,
              int* count);

  // Read a message from the clients publish buffer into the message ring
  int Publish(Client* client, const uint8_t* src, uint64_t src_size);

 private:
  MessageRingConfig config_;
  MemRing mem_ring_;
  MessageStore msg_store_;

  linked::List queue_msgs_;
  linked::List free_msgs_;
  linked::List finished_readers_;

  uint64_t serno_;
  bool initialized_;
};

// Maintains a list of all channels ever seen and efficient lookup from
// channel name to id and channel id to name
class ChannelRegistry {
 public:
  ChannelRegistry(uint64_t max_channels);  // NOLINT(runtime/explicit)

  // Allocate storage
  int Initialize();
  int Deinitialize();

  // Retreive the id associated with the given channel, create a new id if
  // the channel has never been seen before. Return -1 if the maximum number
  // of channels has been exceeded.
  int GetId(const std::string& channel, uint64_t* id, bool* is_new = nullptr);

  // Retreive the channel name for a given channel id, or -1 if the id is
  // not mapped to a channel.
  int GetName(const uint64_t id, std::string* out);

  const std::vector<std::string>& GetChannels() {
    return channels_;
  }

  uint64_t GetFileSize() const {
    return file_ptr_ - file_begin_;
  }

  const char* GetFile() const {
    return file_begin_;
  }

 private:
  uint64_t max_channels_;

  // stores the id -> channel mapping
  // TODO(josh): we should store the channels in a continuous block of memory,
  // with newlines terminating the individual channels. Then we can use
  // StringPiece to reference the individual channels, and we can cat the
  // block of memory directly to the /channels file.
  std::vector<std::string> channels_;

  // stores the channel -> id mapping
  // TODO(josh): switch to efficient pre-allocated rb-tree structure, trie,
  // or other structure that has better performance, or occasionally optimize
  // the runtime index into an optimal/flat structure for better performance.
  std::map<std::string, uint64_t> index_;

  bool initialized_;
  char* file_begin_;
  char* file_end_;
  char* file_ptr_;
};

struct Subscription {
  // TODO(josh): use a fixed size channel string buffer so that re2 uses
  // StringPiece to reference it and doesn't have to re-allocate it's own
  // storage... or something. Need to figure out re2 storage situation

  // unique identifier for this subscription
  uint64_t id;

  // the client that owns this subscription
  Client* client;

  // re2 pattern for matching channel strings
  std::unique_ptr<re2::RE2> pattern;

  // location in either the active or inactive subscription list
  linked::Node node;

  // location in the client's subscription list
  linked::Node client_node;

  // Add channel id subscriptions for each channel that matches this
  // subscription
  int MatchChannels(const std::vector<std::string>& channels, Client* client);
};

class SubscriptionManager {
 public:
  SubscriptionManager(uint64_t max_subscriptions);  // NOLINT(runtime/explicit)

  // Allocate subscription objects, initialize lists, assign ids
  int Initialize();

  // Clear storage and lists
  int Deinitialize();

  // Return all subscriptions from this client back to the pool
  int ReleaseSubscriptions(Client* client);

  // Create a new subscription for the client with the given channel pattern
  // Note that this does not active actual channel subscription bits, that
  // must be done separately using the returned subscription object, matched
  // against all known channels.
  Subscription* AddSubscription(Client* client, const std::string& channel);

  // Return a subscription object back to the pool
  int ReleaseSubscription(Subscription* sub);

  // Go through all active subscription objects, matching their channel patterns
  // against the given channel. If the pattern matches, then add that channel
  // to the set of subscribed channels for the client associated with that
  // subscription
  int NotifyNewChannel(const uint64_t channel_id, const std::string& channel);

  // Exchange a subscription id for a pointer to the associated subscription
  // object
  int ExchangeToken(const uint64_t id, Subscription** sub);

 private:
  bool initialized_;
  uint64_t max_subscriptions_;
  linked::List active_subscriptions_;
  linked::List inactive_subscriptions_;

  // pre-allocated storage for subscription objects
  std::vector<Subscription> subscription_obs_;
};

class ClientManager {
 public:
  ClientManager(uint64_t max_clients);  // NOLINT(runtime/explicit)

  // Allocate client objects, initialize lists, assign ids
  int Initialize();

  // Clear storage and lists
  int Deinitialize();

  // return an unused client object or nullptr if they are all in use.
  Client* CreateClient();

  // Release a client back to the object pool
  int FreeClient(Client* client);

  // Exchange a client id for a pointer to the associated client object
  int ExchangeToken(const uint64_t id, Client** client);

 private:
  bool initialized_;
  uint64_t max_clients_;
  linked::List free_clients_;

  // pre-allocated storage for client objects
  std::vector<Client> client_obs_;
};

}  // namespace fusebus
