=========
Changelog
=========

v 0.1.0

Initial commit. Working initial implementation of all the necessary components.
The fuse-interfacing main is a little disorganized and there's a few outstanding
issues and features that need to be addressed but the tests are all working
so far.

Backend/Server:

* Fixed-size mmap()ed memory buffers for message data storage and message header
  storage. Linked list free pools for each.
* Message data memory buffer is a ring buffer: data is allocated out of the
  "front" and freed out of the "back" of the ring.
* Client manager maps fuse file handles to client objects
* Active client subscriptions are cached in a bitset, with a linked list of
  subscription objects as the record of truth.
* Outstanding poll() requests are cached in client objects so they can be
  serviced on message publishing
* Channels are stored in a simple channel registry using c++ stdlib data
  structures (vector, map).
* Channel file is an in-memory string maintained in parallel to the data
  structures for the registry.
* Subscription objects store an re2::RE2 pattern object.
* Subscription manager maintains a list of all active subscriptions so that it
  can update subscription caches whenever a new channel is encountered
* Message objects contain a linked list of clients currently waiting to read
  that message. Whenever a message is dropped from the front of the queue
  all waiting clients are moved forward to their next message.

Frontend/Client:

* Clients subscribe to channel (regex) patterns by providing a callback
  function accepting a ``RecvMessage`` (contains data buffer pointers and
  message metadata).
* Subscriptions and channels are linked together through an intermediate
  linked-list of "link" objects. (link, link... link). This allows O(1)
  enumeration of channels matching a particular subscription, or subscriptions
  for which a channel matches (i.e. an efficient sparse represetnation of the
  matrix of ``subscriptions`` X ``channels``).
* Uses primarily c++ standard library containers: ``vector``, ``map``, but
  uses internal linked-list for efficiency.
* Publish can either be done with gather/scatter ``writev()`` or by allocating
  message objects with enough prefix-buffer memory for the library to fill
  the header (allowing for a single regular ``write()``
