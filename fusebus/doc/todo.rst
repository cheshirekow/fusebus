====
TODO
====

* Add support to publish a message using a string for the channel name, rather
  than a numeric id. Do the lookup to numeric id on the client side.
* Implement poll() support for the channels file, so that clients can do
  regex matching on client-side if they want, or just so that they can maintain
  an up-to-date channel map.
* Check on the performance impact of using iov to write data back to the client.
  If fuse is going to copy everything to an intermediate buffer, and not
  directly to the users buffer, then this will have a negative performance
  impact and we're better off just using one syscall pair per receive (i.e.
  only return one message per receive).

  * Looking at the libfuse_ sourcecode it just ends up calling the glibc writev
    function.
  * Looking at the glibc_ sourcode, it copies all the iov segments into a
    continuous buffer and then calls write()
  * We can eliminate this copy if we store messages in the queue with the
    metadata header and with the fuse header already attached. Then we can
    bypass the fuse wrapper functions and directly implement fuse_write() using
    this unified buffer.
  * Actually, on further investigation, it appears that linux does implement
    a writev() system call and the glibc function referenced above is probably
    just the portable version (i.e. if linux didn't have it). See, for example,
    this `O'Reilly book`_
  * For the same reason, check the performance of Publish() using writev()
    versus requiring that publish includes 16 bytes prior to data for us to
    fill with the header.

* Unsubscribe is broken on the server, since we don't delete the channel from
  bitmask of the client when removing a subscription. Replicate the
  fusebus_client.cc strategy of using channel <-> subscription links to
  maintain the sparse matrix of assignment.
* In fusebus client, store the offset of the channel file that has been
  currently read, so that we can seek directly to the first byte we haven't
  read yet.
* Add fusebus client test.
* Add client/subscription statistics (i.e. dropped message counts, read/write
  counts, bytes transferred, time spent, etc).

.. _libfuse: https://github.com/libfuse/libfuse/blob/master/lib/fuse_lowlevel.c
.. _glibc: https://github.com/lattera/glibc/blob/master/sysdeps/posix/writev.c
.. _`O'Reilly book`: https://www.safaribooksonline.com/library/view/linux-system-programming/9781449341527/ch04.html
