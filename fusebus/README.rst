=======================================
fusebus: FUSE-based message bus for IPC
=======================================

Fusebus is an Inter-Process Communication (IPC) message bus implemented as a
Filesystem in User SpacE (FUSE). It uses an anonymous publish/subscribe
messaging model. When mounted the filesystem exposes two files::

  fusebus/
  ├── channels
  └── fusebus

The ``fusebus`` file is the main message bus file and communication is done with
semantic ``read()`` and ``write()`` operations. Messages are published to the
bus with ``write()`` and read from the bus with ``read()``.

----------------------
Subscription Mechanism
----------------------

Not every message delivered to the bus is received by every client. Only
messages on channels that are ``subscribe()`` ed by the client will be received.
Subscriptions are created/removed using ``write()`` with a special header
indicating subscription modification.

Subscriptions are specified as string channel name or as a regex pattern of
channels to match. When a client calls ``read()`` on the ``fusebus`` file it
will be delivered the first message it has not yet received matching any
outstanding subscriptions it has registered.

----------------
Channel Registry
----------------

Each message published to the bus is associated with a "channel". Message
channels are semantically strings but are represented internally as
``uint64_t`` numerical ids. When messages are delieved to the client they are
prefixed by a metadata header which contains this numeric id.

The ``channels`` file maps these numeric identifiers to their string names. A
new channel can be registered by writting the channel string to the ``channels``
file. If the string is new it will be appended as a new line to the file. If
the string is already a known channel it will not be added. Channel names are
assigned numeric identifiers in the serial order in which they are added.
Channels cannot be removed for the lifetime of the bus instance.

There is a practical but arbitrary limit on the number of channels the bus can
accomodate. This is determined at compile-time and is currently set at 1024.

--------
Metadata
--------

Every message delived by the bus through ``read()`` is prefixed by a fixed
length message header with the following specification (in ``c`` syntax)::

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


-----
Usage
-----

The fusebus server is started from the command line by the ``fusebus`` command::

    ~$ fusebus /tmp/fusebus-demo
    ~$ tree /tmp/fusebus-demo
    /tmp/fusebus-demo/
    ├── channels
    └── fusebus

    0 directories, 2 files

There is a ``C++`` client library in ``fusebus_client.h``. It can be used like
the following

.. code:: cpp

    fusebus::Fusebus client(busdir_);
    uint64_t channel_a;
    uint64_t channel_b;

    client.GetChannel("CHANNEL_A", &channel_a);
    client.GetChannel("CHANNEL_B", &channel_b);

    auto handler = [](const fusebus::RecvMessage& msg) {
      std::cout << "Message received on channel: " << msg.meta.channel << "\n"
                << std::string(reinterpret_cast<const char*>(msg.data),
                                                            msg.meta.size));
                << "\n";
    };

    client.Subscribe("CHANNEL_.*", handler);

    std::string message = "Hello World!";
    client.Publish(channel_a, reinterpret_cast<uint8_t*>(&message[0]),
                   message.size());
    client.Handle();
    std::string message = "Goodbye!";
    client.Publish(channel_b, reinterpret_cast<uint8_t*>(&message[0]),
                   message.size());
    client.Handle();
  };



