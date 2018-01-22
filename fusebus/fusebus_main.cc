// Copyright 2018 Josh Biakowski <josh.bialkowski@gmail.com>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

// NOTE(josh): must define before including fuse
#define FUSE_USE_VERSION 29
#include <fuse/fuse_lowlevel.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include "fusebus/fusebus.h"

static const char* hello_str = "Hello World!\n";
static const char* busfile_name = "fusebus";
static const char* chanfile_name = "channels";

enum Inodes {
  ROOTDIR_INODE = 1,
  BUSFILE_INODE = 2,
  CHANFILE_INODE = 3,
};

// Main ServerContext for the fusebus instance. This is the ``void* userdata``
// stored by the fuse api.
class ServerContext {
 public:
  ServerContext()
      : channels_(fusebus::MAX_CHANNELS),
        subscription_mgr_(fusebus::MAX_SUBSCRIPTIONS),
        client_mgr_(fusebus::MAX_CLIENTS),
        msg_ring_({.data_size_req = fusebus::MAX_MESSAGE_STORE,
                   .max_messages = fusebus::MAX_MESSAGES,
                   .max_msg_size = fusebus::MAX_MSG_SIZE,
                   .max_clients = fusebus::MAX_CLIENTS}) {}

  int Initialize() {
    int err = channels_.Initialize();
    if (err) {
      dprintf(STDERR_FILENO, "Failed to initialize channel registry: %d\n",
              err);
      return -1;
    }
    err = subscription_mgr_.Initialize();
    if (err) {
      dprintf(STDERR_FILENO, "Failed to initialize subscription manager: %d\n",
              err);
      return -1;
    }
    err = client_mgr_.Initialize();
    if (err) {
      dprintf(STDERR_FILENO, "Failed to initialize client manager: %d\n", err);
      return -1;
    }
    err = msg_ring_.Initialize();
    if (err) {
      dprintf(STDERR_FILENO, "Failed to initialize message ring: %d\n", err);
      return -1;
    }
    return 0;
  }

  int Deinitialize() {
    int err = 0;
    err |= channels_.Deinitialize();
    err |= subscription_mgr_.Deinitialize();
    err |= client_mgr_.Deinitialize();
    err |= msg_ring_.Deinitialize();
    if (err) {
      return -1;
    }
    return 0;
  }

  int stat(fuse_ino_t ino, struct stat* stbuf) {
    stbuf->st_ino = ino;
    switch (ino) {
      case ROOTDIR_INODE:
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        break;

      case BUSFILE_INODE:
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(hello_str);
        break;

      case CHANFILE_INODE:
        stbuf->st_mode = S_IFREG | 0664;
        stbuf->st_nlink = 1;
        stbuf->st_size = channels_.GetFileSize();
        break;

      default:
        return -1;
    }
    return 0;
  }

  int ReplyChannels(fuse_req_t req, off_t off, size_t maxsize) {
    if (off < channels_.GetFileSize()) {
      return fuse_reply_buf(req, channels_.GetFile() + off,
                            std::min(channels_.GetFileSize() - off, maxsize));
    } else {
      return fuse_reply_buf(req, NULL, 0);
    }
  }

  int WriteChannels(fuse_req_t req, const char* buf, size_t size) {
    std::stringstream stream;
    stream.write(buf, size);
    std::string line;
    const uint64_t start_size = channels_.GetFileSize();
    int err = 0;
    while (std::getline(stream, line)) {
      if (line.size() < fusebus::MAX_CHANNEL_LEN) {
        uint64_t id = 0;
        bool is_new = false;
        err = channels_.GetId(line, &id, &is_new);
        if (err) {
          break;
        }
        if (is_new) {
          subscription_mgr_.NotifyNewChannel(id, line);
        }
      } else {
        err = -1;
        break;
      }
    }
    fuse_reply_write(req, size);
    return err;
  }

 public:
  std::mutex mutex_;
  fusebus::ChannelRegistry channels_;
  fusebus::SubscriptionManager subscription_mgr_;
  fusebus::ClientManager client_mgr_;
  fusebus::MessageRing msg_ring_;
};

static void fusebus_getattr(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info* fi) {
  struct stat stbuf;

  (void)fi;

  void* userdata = fuse_req_userdata(req);
  ServerContext* server = reinterpret_cast<ServerContext*>(userdata);

  memset(&stbuf, 0, sizeof(stbuf));
  if (server->stat(ino, &stbuf) == -1)
    fuse_reply_err(req, ENOENT);
  else
    fuse_reply_attr(req, &stbuf, 1.0);
}

static void fusebus_lookup(fuse_req_t req, fuse_ino_t parent,
                           const char* name) {
  struct fuse_entry_param e;

  void* userdata = fuse_req_userdata(req);
  ServerContext* server = reinterpret_cast<ServerContext*>(userdata);

  if (parent != ROOTDIR_INODE) {
    fuse_reply_err(req, ENOENT);
  } else if (strcmp(name, busfile_name) == 0) {
    memset(&e, 0, sizeof(e));
    e.ino = BUSFILE_INODE;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    server->stat(e.ino, &e.attr);

    fuse_reply_entry(req, &e);
  } else if (strcmp(name, chanfile_name) == 0) {
    memset(&e, 0, sizeof(e));
    e.ino = CHANFILE_INODE;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    server->stat(e.ino, &e.attr);
    fuse_reply_entry(req, &e);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

struct dirbuf {
  char* p;
  size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf* b, const char* name,
                       fuse_ino_t ino) {
  struct stat stbuf;
  size_t oldsize = b->size;
  b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
  b->p = (char*)realloc(b->p, b->size);  // NOLINT(readability/casting)
  memset(&stbuf, 0, sizeof(stbuf));
  stbuf.st_ino = ino;
  fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
                    b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char* buf, size_t bufsize,
                             off_t off, size_t maxsize) {
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

static void fusebus_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                            off_t off, struct fuse_file_info* fi) {
  (void)fi;

  if (ino != ROOTDIR_INODE) {
    fuse_reply_err(req, ENOTDIR);
  } else {
    struct dirbuf b;
    memset(&b, 0, sizeof(b));
    dirbuf_add(req, &b, ".", ROOTDIR_INODE);
    dirbuf_add(req, &b, "..", ROOTDIR_INODE);
    dirbuf_add(req, &b, busfile_name, BUSFILE_INODE);
    dirbuf_add(req, &b, chanfile_name, CHANFILE_INODE);
    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
  }
}

static void fusebus_open(fuse_req_t req, fuse_ino_t ino,
                         struct fuse_file_info* fi) {
  if (ino == ROOTDIR_INODE) {
    fuse_reply_err(req, EISDIR);
  } else if (ino == BUSFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    fusebus::Client* client = server->client_mgr_.CreateClient();
    if (client) {
      server->msg_ring_.RegisterClient(client);
      fi->direct_io = 1;
      fi->fh = client->id;
      dprintf(STDERR_FILENO, "Assigning client to fh %lu\n", fi->fh);
      fuse_reply_open(req, fi);
    } else {
      fuse_reply_err(req, ENOMEM);
    }
  } else if (ino == CHANFILE_INODE) {
    // TODO(josh): idea: only allow this file to be opened in O_APPEND mode,
    // for semantic niceness. Alternatively, use cuse_lowlevel instead of
    // fuse_lowlevel (always directIO and simultaneous read/write).
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    fi->direct_io = 1;
    fuse_reply_open(req, fi);
  } else {
    fuse_reply_err(req, EACCES);
  }
}

static void fusebus_release(fuse_req_t req, fuse_ino_t ino,
                            struct fuse_file_info* fi) {
  void* userdata = fuse_req_userdata(req);
  ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
  if (ino == BUSFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    fusebus::Client* client = nullptr;
    if (server->client_mgr_.ExchangeToken(fi->fh, &client)) {
      dprintf(STDERR_FILENO, "Unexpected fileno %lu\n", fi->fh);
    } else {
      dprintf(STDERR_FILENO, "Releasing client number %lu\n", fi->fh);
      server->subscription_mgr_.ReleaseSubscriptions(client);
      server->client_mgr_.FreeClient(client);
      server->msg_ring_.UnregisterClient(client);
    }
  }
}

static void fusebus_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                         struct fuse_file_info* fi) {
  (void)fi;

  if (ino == BUSFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    fusebus::Client* client = nullptr;
    if (server->client_mgr_.ExchangeToken(fi->fh, &client)) {
      dprintf(STDERR_FILENO, "Failed to retrieve client %lu\n", fi->fh);
      fuse_reply_err(req, EACCES);
    } else {
      struct iovec iov[20];
      int count = 20;
      int bytes = server->msg_ring_.Receive(client, size, iov, &count);
      fuse_reply_iov(req, iov, count);
    }
  } else if (ino == CHANFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    server->ReplyChannels(req, off, size);
  } else {
    fuse_reply_err(req, EACCES);
  }
}

static void fusebus_write(fuse_req_t req, fuse_ino_t ino, const char* buf,
                          size_t size, off_t off, struct fuse_file_info* fi) {
  if (ino == CHANFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    server->WriteChannels(req, buf, size);
  } else if (ino == BUSFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    if (size < sizeof(uint64_t)) {
      fuse_reply_err(req, EPERM);
      return;
    }
    fusebus::Client* client = nullptr;
    if (server->client_mgr_.ExchangeToken(fi->fh, &client)) {
      fuse_reply_err(req, EPERM);
      return;
    }

    uint64_t write_type = *reinterpret_cast<const uint64_t*>(buf);
    switch (write_type) {
      case fusebus::WRITE_SUBSCRIBE: {
        std::string channel_pattern(buf + sizeof(uint64_t),
                                    size - sizeof(uint64_t));
        fusebus::Subscription* subs =
            server->subscription_mgr_.AddSubscription(client, channel_pattern);
        subs->MatchChannels(server->channels_.GetChannels(), client);
        if (subs) {
          dprintf(STDERR_FILENO,
                  "Subscribed client %lu channel %s with subscription id %lu\n",
                  client->id, channel_pattern.c_str(), subs->id);
          fuse_reply_write(req, subs->id);
        } else {
          fuse_reply_err(req, ENOMEM);
        }
        break;
      }

      case fusebus::WRITE_UNSUBSCRIBE: {
        uint64_t subs_id =
            *reinterpret_cast<const uint64_t*>(buf + sizeof(uint64_t));
        fusebus::Subscription* subs = nullptr;
        if (server->subscription_mgr_.ExchangeToken(subs_id, &subs)) {
          dprintf(STDERR_FILENO,
                  "Client %lu failed to exchange subscription id %lu"
                  " for pointer\n",
                  client->id, subs_id);
          fuse_reply_err(req, EPERM);
        } else {
          server->subscription_mgr_.ReleaseSubscription(subs);
          dprintf(STDERR_FILENO, "Client %lu removed subscription id %lu\n",
                  client->id, subs_id);
          fuse_reply_write(req, 0);

          // TODO(josh): recompute the list of channels this client is
          // subscribed to
        }
        break;
      }

      case fusebus::WRITE_MESSAGE: {
        int err = server->msg_ring_.Publish(
            client, reinterpret_cast<const uint8_t*>(buf + sizeof(uint64_t)),
            size - sizeof(uint64_t));
        if (err) {
          dprintf(STDERR_FILENO, "Client %lu failed to write data: %d\n",
                  client->id, err);
          fuse_reply_err(req, EPERM);
        } else {
          uint64_t channel =
              *reinterpret_cast<const uint64_t*>(buf + sizeof(uint64_t));
          fuse_reply_write(req, size);
        }
        break;
      }

      default:
        dprintf(STDERR_FILENO, "Client %lu sent unrecognized request %lu\n",
                client->id, write_type);
        fuse_reply_err(req, EPERM);
        break;
    }
  } else {
    fuse_reply_err(req, EACCES);
  }
}

static void fusebus_setattr(fuse_req_t req, fuse_ino_t ino, struct stat* attr,
                            int to_set, struct fuse_file_info* fi) {
  if (ino == CHANFILE_INODE || ino == BUSFILE_INODE) {
    if (to_set & ~FUSE_SET_ATTR_SIZE) {
      // If anything other than size is being set than we reject the request
      fuse_reply_err(req, EACCES);
    }
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    struct stat stbuf;
    memset(&stbuf, 0, sizeof(stbuf));
    server->stat(CHANFILE_INODE, &stbuf);

    // NOTE(josh): we lie and tell the requester that we have resized the file
    // to the requested size.
    stbuf.st_size = attr->st_size;
    fuse_reply_attr(req, &stbuf, 1.0);
  } else {
    fuse_reply_err(req, EACCES);
  }
}

static void fusebus_poll(fuse_req_t req, fuse_ino_t ino,
                         struct fuse_file_info* fi,
                         struct fuse_pollhandle* ph) {
  if (ino == BUSFILE_INODE) {
    void* userdata = fuse_req_userdata(req);
    ServerContext* server = reinterpret_cast<ServerContext*>(userdata);
    fusebus::Client* client = nullptr;
    if (server->client_mgr_.ExchangeToken(fi->fh, &client)) {
      dprintf(STDERR_FILENO,
              "Failed to exchange client token %lu for pointer\n", fi->fh);
      fuse_reply_err(req, EACCES);
      if (ph) {
        fuse_pollhandle_destroy(ph);
      }
    } else {
      if (client->msg) {
        fuse_reply_poll(req, POLLIN | POLLOUT);
        if (ph) {
          fuse_lowlevel_notify_poll(ph);
          fuse_pollhandle_destroy(ph);
        }
      } else {
        fuse_reply_poll(req, POLLOUT);
        client->poll_handle.reset(ph);
      }
    }
  } else if (ino == CHANFILE_INODE) {
    // TODO(josh): implement polling for channel file so that clients know
    // when to update their channel lists. This will also allow them to only
    // read in new channels.
    fuse_lowlevel_notify_poll(ph);
    fuse_pollhandle_destroy(ph);
  } else {
    fuse_reply_err(req, EACCES);
  }
}

static struct fuse_lowlevel_ops fusebus_oper = {};

int main(int argc, char* argv[]) {
  fusebus_oper.lookup = fusebus_lookup;
  fusebus_oper.getattr = fusebus_getattr;
  fusebus_oper.setattr = fusebus_setattr;
  fusebus_oper.readdir = fusebus_readdir;
  fusebus_oper.open = fusebus_open;
  fusebus_oper.poll = fusebus_poll;
  fusebus_oper.read = fusebus_read;
  fusebus_oper.write = fusebus_write;
  fusebus_oper.release = fusebus_release;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_chan* ch;
  char* mountpoint;
  int err = -1;

  ServerContext ctx;
  if (ctx.Initialize()) {
    dprintf(STDERR_FILENO, "Failed to initialize server context\n");
    return 1;
  }

  if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
      (ch = fuse_mount(mountpoint, &args)) != NULL) {
    struct fuse_session* se;

    se = fuse_lowlevel_new(&args, &fusebus_oper, sizeof(fusebus_oper), &ctx);
    if (se != NULL) {
      if (fuse_set_signal_handlers(se) != -1) {
        fuse_session_add_chan(se, ch);
        err = fuse_session_loop(se);
        fuse_remove_signal_handlers(se);
        fuse_session_remove_chan(ch);
      }
      fuse_session_destroy(se);
    }
    fuse_unmount(mountpoint, ch);
  }
  fuse_opt_free_args(&args);

  ctx.Deinitialize();
  return err ? 1 : 0;
}
