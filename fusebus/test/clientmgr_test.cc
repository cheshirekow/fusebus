// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <gtest/gtest.h>

#include "fusebus/fusebus.h"

namespace std {

// http://herbsutter.com/gotw/_102/
template <typename T, typename... U>
std::unique_ptr<T> make_unique(U&&... args) {
  return std::unique_ptr<T>(new T(std::forward<U>(args)...));
}

}  // namespace std

TEST(ChannelRegistryTest, AddCheckTest) {
  fusebus::ChannelRegistry registry(4);
  uint64_t chanid = 0;
  ASSERT_EQ(0, registry.Initialize());

  EXPECT_EQ(0, registry.GetId("Channel 1", &chanid));
  EXPECT_EQ(0, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 2", &chanid));
  EXPECT_EQ(1, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 3", &chanid));
  EXPECT_EQ(2, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 4", &chanid));
  EXPECT_EQ(3, chanid);
  EXPECT_EQ(-4, registry.GetId("Channel 5", &chanid));

  EXPECT_EQ(0, registry.GetId("Channel 1", &chanid));
  EXPECT_EQ(0, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 2", &chanid));
  EXPECT_EQ(1, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 3", &chanid));
  EXPECT_EQ(2, chanid);
  EXPECT_EQ(0, registry.GetId("Channel 4", &chanid));
  EXPECT_EQ(3, chanid);

  std::string chan;
  EXPECT_EQ(0, registry.GetName(0, &chan));
  EXPECT_EQ("Channel 1", chan);
  EXPECT_EQ(0, registry.GetName(1, &chan));
  EXPECT_EQ("Channel 2", chan);
  EXPECT_EQ(0, registry.GetName(2, &chan));
  EXPECT_EQ("Channel 3", chan);
  EXPECT_EQ(0, registry.GetName(3, &chan));
  EXPECT_EQ("Channel 4", chan);
  EXPECT_EQ(-1, registry.GetName(4, &chan));

  EXPECT_EQ(0, registry.Deinitialize());
}

TEST(SubscriptionManagerTest, ChannelNotifyAddsSubscriptions) {
  fusebus::ChannelRegistry channels(5);
  fusebus::SubscriptionManager subs_mgr(3);
  ASSERT_EQ(0, channels.Initialize());
  ASSERT_EQ(0, subs_mgr.Initialize());

  fusebus::Client client_a;
  fusebus::InitClient(&client_a);
  client_a.id = 1;

  fusebus::Client client_b;
  fusebus::InitClient(&client_b);
  client_b.id = 2;

  fusebus::Subscription* sub1 =
      subs_mgr.AddSubscription(&client_a, "CHANNEL A");
  EXPECT_NE(nullptr, sub1);
  fusebus::Subscription* sub2 =
      subs_mgr.AddSubscription(&client_a, "^CHANNEL .*$");
  EXPECT_NE(nullptr, sub2);
  fusebus::Subscription* sub3 = subs_mgr.AddSubscription(&client_b, ".*");
  EXPECT_NE(nullptr, sub3);
  EXPECT_EQ(nullptr, subs_mgr.AddSubscription(&client_a, "Doesn't matter"));

  EXPECT_TRUE(client_a.subscriptions.None());
  EXPECT_TRUE(client_b.subscriptions.None());

  uint64_t id = 0;

  EXPECT_EQ(0, channels.GetId("CHANNEL A", &id));
  EXPECT_EQ(0, id);
  subs_mgr.NotifyNewChannel(id, "CHANNEL A");
  EXPECT_TRUE(client_a.subscriptions[0]);
  EXPECT_FALSE(client_a.subscriptions[1]);
  EXPECT_FALSE(client_a.subscriptions[2]);
  EXPECT_TRUE(client_b.subscriptions[0]);
  EXPECT_FALSE(client_b.subscriptions[1]);
  EXPECT_FALSE(client_b.subscriptions[2]);

  EXPECT_EQ(0, channels.GetId("CHANNEL B", &id));
  EXPECT_EQ(1, id);
  subs_mgr.NotifyNewChannel(id, "CHANNEL B");
  EXPECT_TRUE(client_a.subscriptions[0]);
  EXPECT_TRUE(client_a.subscriptions[1]);
  EXPECT_FALSE(client_a.subscriptions[2]);
  EXPECT_TRUE(client_b.subscriptions[0]);
  EXPECT_TRUE(client_b.subscriptions[1]);
  EXPECT_FALSE(client_b.subscriptions[2]);

  EXPECT_EQ(0, channels.GetId("CHANEL", &id));
  EXPECT_EQ(2, id);
  subs_mgr.NotifyNewChannel(id, "CHANEL");
  EXPECT_TRUE(client_a.subscriptions[0]);
  EXPECT_TRUE(client_a.subscriptions[1]);
  EXPECT_FALSE(client_a.subscriptions[2]);
  EXPECT_TRUE(client_b.subscriptions[0]);
  EXPECT_TRUE(client_b.subscriptions[1]);
  EXPECT_TRUE(client_b.subscriptions[2]);

  fusebus::Subscription* sub;
  EXPECT_EQ(0, subs_mgr.ExchangeToken(0, &sub));
  EXPECT_EQ(sub1, sub);
  EXPECT_EQ(0, subs_mgr.ExchangeToken(1, &sub));
  EXPECT_EQ(sub2, sub);
  EXPECT_EQ(0, subs_mgr.ExchangeToken(2, &sub));
  EXPECT_EQ(sub3, sub);
  EXPECT_EQ(-1, subs_mgr.ExchangeToken(3, &sub));

  EXPECT_EQ(0, subs_mgr.ReleaseSubscription(sub1));
  EXPECT_EQ(0, subs_mgr.ReleaseSubscription(sub2));
  EXPECT_EQ(0, subs_mgr.ReleaseSubscriptions(&client_b));
  EXPECT_EQ(nullptr, sub3->client);

  EXPECT_EQ(0, channels.Deinitialize());
  EXPECT_EQ(0, subs_mgr.Deinitialize());
}

TEST(ClientMgrTest, AllocFreeTest) {
  fusebus::ClientManager mgr(2);
  ASSERT_EQ(0, mgr.Initialize());

  fusebus::Client* a = mgr.CreateClient();
  ASSERT_NE(nullptr, a);
  fusebus::Client* b = mgr.CreateClient();
  ASSERT_NE(nullptr, b);
  EXPECT_EQ(nullptr, mgr.CreateClient());

  fusebus::Client* client;
  EXPECT_EQ(0, mgr.ExchangeToken(0, &client));
  EXPECT_EQ(a, client);
  EXPECT_EQ(0, mgr.ExchangeToken(1, &client));
  EXPECT_EQ(b, client);
  EXPECT_EQ(-1, mgr.ExchangeToken(3, &client));

  EXPECT_EQ(0, mgr.FreeClient(a));
  EXPECT_EQ(0, mgr.FreeClient(b));

  fusebus::Client c;
  EXPECT_EQ(-1, mgr.FreeClient(&c));

  EXPECT_EQ(0, mgr.Deinitialize());
}
