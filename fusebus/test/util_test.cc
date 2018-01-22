// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "fusebus/fusebus.h"

struct TestNode {
  int value;
  fusebus::linked::Node node_a;
  fusebus::linked::Node node_b;
};

enum { NUM_NODES = 10 };

TEST(LinkedTest, NodeTest) {
  fusebus::linked::Node a{};
  fusebus::linked::Node b{};
  fusebus::linked::Node c{};

  c.InsertAfter(&b);
  EXPECT_EQ(nullptr, b.prev);
  EXPECT_EQ(&c, b.next);
  EXPECT_EQ(&b, c.prev);
  EXPECT_EQ(nullptr, c.next);

  a.InsertBefore(&b);
  EXPECT_EQ(nullptr, a.prev);
  EXPECT_EQ(&a, b.prev);
  EXPECT_EQ(&b, a.next);
  EXPECT_EQ(&b, c.prev);
  EXPECT_EQ(&c, b.next);
  EXPECT_EQ(nullptr, c.next);

  b.Remove();
  EXPECT_EQ(nullptr, a.prev);
  EXPECT_EQ(&c, a.next);
  EXPECT_EQ(&a, c.prev);
  EXPECT_EQ(nullptr, c.next);
  EXPECT_EQ(nullptr, b.prev);
  EXPECT_EQ(nullptr, b.next);
}

TEST(LinkedTest, ListTest) {
  std::array<TestNode, NUM_NODES> nodes;
  memset(&nodes[0], 0, sizeof(nodes));

  fusebus::linked::List forward_list;
  fusebus::linked::List reverse_list;
  for (size_t idx = 0; idx < NUM_NODES; ++idx) {
    nodes[idx].value = idx;
    forward_list.PushBack(&nodes[idx].node_a);
    reverse_list.PushFront(&nodes[idx].node_b);
  }

  std::array<int, NUM_NODES> forward_values = {-1, -1, -1, -1, -1,
                                               -1, -1, -1, -1, -1};
  std::array<int, NUM_NODES> forward_expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

  size_t idx = 0;
  for (auto iter = forward_list.begin(); iter != forward_list.end(); ++iter) {
    forward_values[idx++] =
        fusebus::container_of((*iter), &TestNode::node_a)->value;
  }

  EXPECT_EQ(forward_expected, forward_values);

  idx = 0;
  std::array<int, NUM_NODES> reverse_values = {-1, -1, -1, -1, -1,
                                               -1, -1, -1, -1, -1};
  std::array<int, NUM_NODES> reverse_expected = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
  for (auto iter = forward_list.rbegin(); iter != forward_list.end(); --iter) {
    reverse_values[idx++] =
        fusebus::container_of((*iter), &TestNode::node_a)->value;
  }
  EXPECT_EQ(reverse_expected, reverse_values);

  idx = 0;
  for (auto iter = reverse_list.begin(); iter != reverse_list.end(); ++iter) {
    reverse_values[idx++] =
        fusebus::container_of((*iter), &TestNode::node_b)->value;
  }
  EXPECT_EQ(reverse_expected, reverse_values);

  idx = 0;
  forward_values = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  reverse_values = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  for (fusebus::linked::Node* node : forward_list) {
    forward_values[idx++] =
        fusebus::container_of(node, &TestNode::node_a)->value;
  }
  EXPECT_EQ(forward_expected, forward_values);
}

TEST(LinkedTest, RingTest) {
  TestNode a{0};
  TestNode b{1};
  TestNode c{2};

  fusebus::linked::Ring ring;
  EXPECT_FALSE(ring);

  ring.Push(&a.node_a);
  EXPECT_TRUE(ring);

  ring.Push(&b.node_a);
  EXPECT_TRUE(ring);

  ring.Push(&c.node_a);
  EXPECT_TRUE(ring);

  EXPECT_EQ(a.node_a.next, &b.node_a);
  EXPECT_EQ(b.node_a.next, &c.node_a);
  EXPECT_EQ(c.node_a.next, a.node_a.prev);
  EXPECT_EQ(c.node_a.prev, &b.node_a);
  EXPECT_EQ(b.node_a.prev, &a.node_a);

  EXPECT_EQ(&a.node_a, ring.Pop());
  EXPECT_EQ(&b.node_a, ring.Pop());
  EXPECT_EQ(&c.node_a, ring.Pop());

  EXPECT_FALSE(ring);
}
