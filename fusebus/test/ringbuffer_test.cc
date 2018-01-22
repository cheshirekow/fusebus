// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <gtest/gtest.h>

#include "fusebus/fusebus.h"

TEST(MemRingTest, SimpleAllocDealloc) {
  fusebus::MemRing memring(1024);
  ASSERT_EQ(0, memring.Initialize());
  fusebus::MemBlock block_a;
  fusebus::MemBlock block_b;
  fusebus::MemBlock block_c;
  fusebus::MemBlock block_d;

  ASSERT_EQ(4096, memring.GetFreeSize());
  EXPECT_EQ(0, memring.RequestAllocation(&block_a, 100));
  EXPECT_EQ(100, block_a.size);
  EXPECT_EQ(112, block_a.end - block_a.begin);
  ASSERT_EQ(3984, memring.GetFreeSize());
  EXPECT_EQ(0, memring.RequestAllocation(&block_b, 3960));
  EXPECT_EQ(3960, block_b.size);
  EXPECT_EQ(3968, block_b.end - block_b.begin);
  ASSERT_EQ(16, memring.GetFreeSize());
  EXPECT_EQ(-1, memring.RequestAllocation(&block_c, 100));
  EXPECT_EQ(-3, memring.ReleaseAllocation(&block_b));
  EXPECT_EQ(0, memring.ReleaseAllocation(&block_a));
  EXPECT_EQ(0, memring.RequestAllocation(&block_c, 100));
  EXPECT_EQ(16, block_c.size);
  EXPECT_EQ(16, block_c.end - block_c.begin);
  EXPECT_EQ(0, memring.RequestAllocation(&block_d, 94));
  EXPECT_EQ(94, block_d.size);
  EXPECT_EQ(96, block_d.end - block_d.begin);
  EXPECT_EQ(0, memring.ReleaseAllocation(&block_b));
  EXPECT_EQ(0, memring.ReleaseAllocation(&block_c));
  EXPECT_EQ(4000, memring.GetFreeSize());

  ASSERT_EQ(0, memring.Deinitialize());
}

TEST(MessageStoreTest, PushPopTest) {
  fusebus::MessageStore store(10);
  ASSERT_GT(10000, store.GetMaxMessages());
  ASSERT_EQ(0, store.Initialize());
  fusebus::Message* msg = store.Alloc();
  EXPECT_NE(nullptr, msg);
  store.Free(msg);
  for (uint8_t trial = 0; trial < 3; ++trial) {
    fusebus::linked::List alloc_list;
    for (uint64_t idx = 0; idx < store.GetMaxMessages(); ++idx) {
      fusebus::Message* msg = store.Alloc();
      ASSERT_NE(nullptr, msg) << "Failed to allocate message number " << idx;
      alloc_list.PushBack(&msg->node);
    }
    while (alloc_list) {
      fusebus::Message* msg =
          fusebus::container_of(alloc_list.PopFront(), &fusebus::Message::node);
      store.Free(msg);
    }
    alloc_list.Clear();
  }

  EXPECT_EQ(0, store.Deinitialize());
}

TEST(MessageRingTest, PubSubTest) {
  fusebus::MessageRing bus(
      {.data_size_req = 1024, .max_messages = 100, .max_msg_size = 1000});
  ASSERT_EQ(0, bus.Initialize());

  fusebus::Client client_a{1};
  client_a.subscriptions.Clear();
  client_a.subscriptions[1] = true;
  client_a.subscriptions[2] = true;
  EXPECT_EQ(0, bus.RegisterClient(&client_a));

  fusebus::Client client_b{2};
  client_b.subscriptions.Clear();
  client_b.subscriptions[2] = true;
  client_b.subscriptions[3] = true;
  EXPECT_EQ(0, bus.RegisterClient(&client_b));

  fusebus::Client client_c{3};
  client_c.subscriptions.Clear();
  client_c.subscriptions[3] = true;
  client_c.subscriptions[4] = true;
  EXPECT_EQ(0, bus.RegisterClient(&client_c));

  char input_msgs[4][32] = {
      "xxxxxxxxThis is message 1, xx",  //
      "xxxxxxxxThis is message 2, xx",  //
      "xxxxxxxxThis is message 3, xx",  //
      "xxxxxxxxThis is message 4, xx",
  };

  *reinterpret_cast<uint64_t*>(input_msgs[0]) = 1;
  *reinterpret_cast<uint64_t*>(input_msgs[1]) = 2;
  *reinterpret_cast<uint64_t*>(input_msgs[2]) = 3;
  *reinterpret_cast<uint64_t*>(input_msgs[3]) = 4;

  uint8_t recv_buf[1024];
  fusebus::MessageMeta* meta = nullptr;
  char* msg_data = nullptr;

  EXPECT_EQ(
      0, bus.Publish(&client_a, reinterpret_cast<uint8_t*>(input_msgs[0]), 30));
  EXPECT_EQ(
      0, bus.Publish(&client_b, reinterpret_cast<uint8_t*>(input_msgs[1]), 30));

  ASSERT_NE(nullptr, client_a.msg);
  EXPECT_EQ(64 + 2 * sizeof(fusebus::MessageMeta),
            bus.Receive(&client_a, recv_buf, 1024));

  meta = reinterpret_cast<fusebus::MessageMeta*>(recv_buf);
  msg_data = reinterpret_cast<char*>(recv_buf) + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(1, meta->channel);
  EXPECT_EQ(1, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 1", std::string(msg_data, 17));

  meta = reinterpret_cast<fusebus::MessageMeta*>(msg_data + meta->stride);
  msg_data = msg_data + meta->stride + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(2, meta->channel);
  EXPECT_EQ(2, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 2", std::string(msg_data, 17));

  EXPECT_EQ(0, bus.Receive(&client_a, recv_buf, 1024));
  EXPECT_EQ(32 + sizeof(fusebus::MessageMeta),
            bus.Receive(&client_b, recv_buf, 1024));

  meta = reinterpret_cast<fusebus::MessageMeta*>(recv_buf);
  msg_data = reinterpret_cast<char*>(recv_buf) + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(2, meta->channel);
  EXPECT_EQ(2, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 2", std::string(msg_data, 17));

  EXPECT_EQ(0, bus.Receive(&client_b, recv_buf, 1024));

  EXPECT_EQ(
      0, bus.Publish(&client_a, reinterpret_cast<uint8_t*>(input_msgs[2]), 30));
  EXPECT_EQ(
      0, bus.Publish(&client_b, reinterpret_cast<uint8_t*>(input_msgs[3]), 30));
  EXPECT_EQ(0, bus.Receive(&client_a, recv_buf, 1024));
  EXPECT_EQ(32 + sizeof(fusebus::MessageMeta),
            bus.Receive(&client_b, recv_buf, 1024));

  meta = reinterpret_cast<fusebus::MessageMeta*>(recv_buf);
  msg_data = reinterpret_cast<char*>(recv_buf) + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(3, meta->channel);
  EXPECT_EQ(1, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 3", std::string(msg_data, 17));

  EXPECT_EQ(64 + 2 * sizeof(fusebus::MessageMeta),
            bus.Receive(&client_c, recv_buf, 1024));

  meta = reinterpret_cast<fusebus::MessageMeta*>(recv_buf);
  msg_data = reinterpret_cast<char*>(recv_buf) + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(3, meta->channel);
  EXPECT_EQ(1, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 3", std::string(msg_data, 17));

  meta = reinterpret_cast<fusebus::MessageMeta*>(msg_data + meta->stride);
  msg_data = msg_data + meta->stride + sizeof(fusebus::MessageMeta);
  EXPECT_EQ(4, meta->channel);
  EXPECT_EQ(2, meta->client);
  EXPECT_EQ(22, meta->size);
  EXPECT_EQ(32, meta->stride);
  EXPECT_EQ("This is message 4", std::string(msg_data, 17));

  EXPECT_EQ(0, bus.UnregisterClient(&client_a));
  EXPECT_EQ(0, bus.UnregisterClient(&client_b));
  EXPECT_EQ(0, bus.UnregisterClient(&client_c));
  EXPECT_EQ(0, bus.Deinitialize());
}

TEST(MessageRingTest, OverflowTest) {
  fusebus::MessageRing bus(
      {.data_size_req = 1024, .max_messages = 1024, .max_msg_size = 1000});
  ASSERT_EQ(0, bus.Initialize());

  fusebus::Client client_a{1};
  client_a.subscriptions.Clear();
  client_a.subscriptions[1] = true;
  client_a.subscriptions[2] = true;
  EXPECT_EQ(0, bus.RegisterClient(&client_a));

  fusebus::Client client_b{2};
  client_b.subscriptions.Clear();
  client_b.subscriptions[2] = true;
  client_b.subscriptions[3] = true;
  EXPECT_EQ(0, bus.RegisterClient(&client_b));

  const int page_size = getpagesize();
  char scratch[128];
  uint64_t expected_msgs = (page_size / 32);
  uint64_t idx = 0;
  for (; idx < 3 * expected_msgs / 2; idx++) {
    *reinterpret_cast<uint64_t*>(scratch) = 1;
    int write_size = snprintf(scratch + 8, sizeof(scratch) - 8,
                              "This is message %d", static_cast<int>(idx));

    EXPECT_EQ(0, bus.Publish(&client_a, reinterpret_cast<uint8_t*>(scratch),
                             8 + write_size));
  }

  *reinterpret_cast<uint64_t*>(scratch) = 2;
  int write_size = snprintf(scratch + 8, sizeof(scratch) - 8,
                            "This is message %d", static_cast<int>(idx));

  EXPECT_EQ(0, bus.Publish(&client_a, reinterpret_cast<uint8_t*>(scratch),
                           8 + write_size));
  EXPECT_EQ(RoundToPage(write_size, fusebus::MAX_ALIGN) +
                sizeof(fusebus::MessageMeta),
            bus.Receive(&client_b, reinterpret_cast<uint8_t*>(scratch),
                        sizeof(scratch)))
      << "  write_size: " << write_size
      << "\n  sizeof(MessageMeta): " << sizeof(fusebus::MessageMeta) << "\n";

  EXPECT_EQ(0, bus.Deinitialize());
}
