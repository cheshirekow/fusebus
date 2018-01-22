"""
Fusebus utility client
"""
from __future__ import print_function

import argparse
import logging
import os
import select
import struct

WRITE_SUBSCRIBE = 0
WRITE_UNSUBSCRIBE = 1
WRITE_MESSAGE = 2

META_FMT = '@QQQQQ'
META_LEN = struct.calcsize(META_FMT)

def do_cat(bus_dir, channels):
  channel_map = []
  with open(os.path.join(bus_dir, 'channels'), 'r') as infile:
    for channel in infile:
      channel_map.append(channel.strip())

  bus_fd = os.open(os.path.join(bus_dir, 'fusebus'), os.O_RDWR)
  subids = []
  for channel in channels:
    subid = os.write(bus_fd, struct.pack('@Q', WRITE_SUBSCRIBE) + channel)
    subids.append(subid)

  buf = os.read(bus_fd, 1024**2)
  while buf:
    (chanid, serno, _, size, stride) \
      = struct.unpack(META_FMT, buf[0:META_LEN])
    data = buf[META_LEN:META_LEN + size]
    print('[{:03d}] {:10s} : {:s}'.format(serno, channel_map[chanid], data))
    buf = buf[META_LEN + stride:]

  for subid in subids:
    os.write(bus_fd, struct.pack('@QQ', WRITE_UNSUBSCRIBE, subid))

def do_follow(bus_dir, channels):
  bus_fd = os.open(os.path.join(bus_dir, 'fusebus'), os.O_RDWR)
  subids = []
  for channel in channels:
    subid = os.write(bus_fd, struct.pack('@Q', WRITE_SUBSCRIBE) + channel)
    subids.append(subid)

  while True:
    try:
      rlist, _, _ = select.select([bus_fd], [], [], 10.0)
      if bus_fd in rlist:
        buf = os.read(bus_fd, 1024**2)
        while buf:
          (chanid, serno, _, size, stride) \
            = struct.unpack(META_FMT, buf[0:META_LEN])
          data = buf[META_LEN:META_LEN + size]
          print('[{:03d}] {:03d} : {:s}'.format(serno, chanid, data))
          buf = buf[META_LEN + stride:]
      else:
        logging.info('poll timeout')
    except KeyboardInterrupt:
      logging.info('Exiting.')
      break

  for subid in subids:
    os.write(bus_fd, struct.pack('@QQ', WRITE_UNSUBSCRIBE, subid))

def do_publish(bus_dir, channel, message):
  with open(os.path.join(bus_dir, 'channels'), 'w') as outfile:
    outfile.write(channel)

  channel_map = {}
  with open(os.path.join(bus_dir, 'channels'), 'r') as infile:
    for idx, line in enumerate(infile):
      channel_map[line.strip()] = idx

  chanid = channel_map.get(channel)
  if chanid is None:
    logging.error('Failed to register channel %s before publish', channel)
  logging.info('Publishing to chanid: %d', chanid)

  bus_fd = os.open(os.path.join(bus_dir, 'fusebus'), os.O_RDWR)
  os.write(bus_fd, struct.pack('@QQ', WRITE_MESSAGE, chanid) + message)

def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('-b', '--bus-dir', help='bus directory', required=True)
  parser.add_argument('-l', '--log-level', default='info',
                      choices=['debug', 'info', 'warning', 'error'])

  subparsers = parser.add_subparsers(dest='command')
  cat_parser = subparsers.add_parser(
      'cat', help='Print messages currently in the queue')
  follow_parser = subparsers.add_parser(
      'follow', help='Print messages as they are published')
  for subparser in [cat_parser, follow_parser]:
    subparser.add_argument('channels', nargs='*')

  pub_parser = subparsers.add_parser(
      'publish', help='Write a string message to the queue')
  pub_parser.add_argument('-c', '--channel', required=True)
  pub_parser.add_argument("message")
  args = parser.parse_args()

  format_str = '%(levelname)-8s %(filename)s [%(lineno)-3s] : %(message)s'
  logging.basicConfig(level=getattr(logging, args.log_level.upper()),
                      format=format_str,
                      datefmt='%Y-%m-%d %H:%M:%S',
                      filemode='w')

  if args.command == 'cat':
    do_cat(args.bus_dir, args.channels)
  elif args.command == 'follow':
    do_follow(args.bus_dir, args.channels)
  elif args.command == 'publish':
    do_publish(args.bus_dir, args.channel, args.message)
  else:
    logging.error('Unrecognized command %s', args.command)



if __name__ == '__main__':
  main()
