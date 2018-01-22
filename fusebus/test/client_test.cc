// Copyright 2018 Josh Bialkowski <josh.bialkowski@gmail.com>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <gtest/gtest.h>

#include "fusebus/fusebus_client.h"

class ClientTest : public ::testing::Test {
  void SetUp() override {
    busdir_ = "fusebus_XXXXXX";
    ASSERT_NE(nullptr, mkdtemp(&busdir_[0])) << "error was: "
                                             << strerror(errno);
    printf("Using tempdir: %s\n", busdir_.c_str());

    server_pid_ = fork();
    if (server_pid_ == 0) {
      int err = execl("../fusebus", "fusebus", busdir_.c_str(), NULL);
      _exit(1);
    }

    // Sleep for 1 second. If fusebus is still alive after that assume that
    // it is good to go.
    usleep(100000);

    int wstatus;
    int err = waitpid(server_pid_, &wstatus, WNOHANG);
    ASSERT_NE(server_pid_, err) << "fusebus server died already! pid: "
                                << server_pid_;
  }

  void TearDown() override {
    if (server_pid_) {
      int wstatus = 0;
      int err = 0;

      int fusermount_pid = fork();
      if (fusermount_pid == 0) {
        int err =
            execl("/bin/fusermount", "fusermount", "-u", busdir_.c_str(), NULL);
        _exit(1);
      }

      err = waitpid(fusermount_pid, &wstatus, 0);
      ASSERT_EQ(fusermount_pid, err)
          << "Failed to wait fusermount process: " << fusermount_pid
          << "\n  err: " << err << ", " << strerror(errno);
      err = waitpid(server_pid_, &wstatus, 0);
      ASSERT_EQ(server_pid_, err)
          << "Failed to wait fusermount process: " << server_pid_
          << "\n  err: " << err << ", " << strerror(errno);
    }

    int err = rmdir(busdir_.c_str());
    if (err) {
      printf("Failed to rmdir tempdir: %s\n", busdir_.c_str());
    }
  }

 protected:
  std::string busdir_;
  int server_pid_;
};

TEST_F(ClientTest, SingleConsumerSingleProducer) {
  fusebus::Fusebus client_a(busdir_);
  fusebus::Fusebus client_b(busdir_);

  uint64_t channel_a;
  uint64_t channel_b;
  uint64_t channel_x;

  ASSERT_EQ(0, client_a.GetChannel("CHANNEL_A", &channel_a));
  ASSERT_EQ(0, client_b.GetChannel("CHANNEL_A", &channel_x));
  ASSERT_EQ(channel_a, channel_x);

  ASSERT_EQ(0, client_b.GetChannel("CHANNEL_B", &channel_b));
  ASSERT_EQ(0, client_a.GetChannel("CHANNEL_B", &channel_x));
  ASSERT_EQ(channel_b, channel_x);

  std::string expected_msg;
  uint64_t expected_channel;
  bool msg_received;

  auto handler = [&](const fusebus::RecvMessage& msg) {
    ASSERT_EQ(expected_channel, msg.meta.channel);
    ASSERT_EQ(expected_msg, std::string(reinterpret_cast<const char*>(msg.data),
                                        msg.meta.size));
    msg_received = true;
  };

  fusebus::ClientSubscription* sub = nullptr;
  client_b.Subscribe("CHANNEL_.*", handler, &sub);
  ASSERT_NE(nullptr, sub);

  expected_channel = channel_a;
  expected_msg = "Hello World";
  std::string publish_buf = "xxxxxxxxXXXXXXXXHello World";

  client_a.PublishPad(expected_channel,
                      reinterpret_cast<uint8_t*>(&publish_buf[0]),
                      publish_buf.size());
  msg_received = false;
  client_b.Handle();
  ASSERT_TRUE(msg_received);

  client_a.Publish(expected_channel,
                   reinterpret_cast<uint8_t*>(&expected_msg[0]),
                   expected_msg.size());
  msg_received = false;
  client_b.Handle();
  ASSERT_TRUE(msg_received);

  for (uint32_t idx = 0; idx < 10000; ++idx) {
    std::stringstream strm;
    strm << "Hello !" << idx;
    expected_msg = strm.str();
    client_a.Publish(expected_channel,
                     reinterpret_cast<uint8_t*>(&expected_msg[0]),
                     expected_msg.size());
    msg_received = false;
    client_b.Handle();
    ASSERT_TRUE(msg_received);
  }
}
