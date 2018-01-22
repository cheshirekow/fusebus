// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <gtest/gtest.h>

#include "fusebus/fusebus.h"

template <typename T>
class BitSetTest : public ::testing::Test {};
typedef ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t> BitSetTypes;

TYPED_TEST_CASE(BitSetTest, BitSetTypes);
TYPED_TEST(BitSetTest, GetSetTest) {
  typedef fusebus::BitSet<6, TypeParam> BitSetT;

  BitSetT bitset;
  bitset.Clear();

  EXPECT_FALSE(bitset.Any());
  EXPECT_FALSE(bitset.All());
  EXPECT_TRUE(bitset.None());

  bitset[3] = true;

  EXPECT_TRUE(bitset.Any());
  EXPECT_FALSE(bitset.All());
  EXPECT_FALSE(bitset.None());

  EXPECT_FALSE(bitset[2]);
  EXPECT_TRUE(bitset[3]);
  EXPECT_FALSE(bitset[4]);

  typedef fusebus::BitSet<20, TypeParam> BitSetT2;

  BitSetT2 bitset2;
  bitset2.Clear();

  EXPECT_FALSE(bitset2.Any());
  EXPECT_FALSE(bitset2.All());
  EXPECT_TRUE(bitset2.None());

  bitset2[18] = true;

  EXPECT_TRUE(bitset2.Any());
  EXPECT_FALSE(bitset2.All());
  EXPECT_FALSE(bitset2.None());

  EXPECT_FALSE(bitset2[17]);
  EXPECT_TRUE(bitset2[18]);
  EXPECT_FALSE(bitset2[19]);
}
