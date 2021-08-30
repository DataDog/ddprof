extern "C" {
#include "statsd.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
}

#include <gtest/gtest.h>
#include <string>

TEST(StatsDTest, Connection) {
  // This takes advantage of the fact that UDP domain sockets opened in the way
  // statsd does it are full-duplex
  const char path_listen[] = "/tmp/my_statsd_listener";
  unlink(path_listen); // Make sure the default listening path is available

  int fd_listener;
  DDRes lres = statsd_listen(path_listen, strlen(path_listen), &fd_listener);
  int fd_client;
  DDRes cres = statsd_connect(path_listen, strlen(path_listen), &fd_client);
  EXPECT_TRUE(IsDDResOK(lres));
  EXPECT_TRUE(IsDDResOK(cres));

  // Cleanup
  close(fd_listener);
  close(fd_client);
  unlink(path_listen);
}

TEST(StatsDTest, BadConnection) {
  // This takes advantage of the fact that UDP domain sockets opened in the way
  // statsd does it are full-duplex
  const char path_listen[] = "/tmp/my_statsd_listener";
  const char path_try[] = "/tmp/my_statsd_Listener";
  unlink(path_listen); // Make sure the default listening path is available
  unlink(path_try);

  int fd_listener;
  DDRes lres = statsd_listen(path_listen, strlen(path_listen), &fd_listener);
  int fd_client;
  DDRes cres = statsd_connect(path_try, strlen(path_try), &fd_client);
  EXPECT_TRUE(IsDDResOK(lres));
  EXPECT_FALSE(IsDDResOK(cres));

  // Cleanup
  close(fd_listener);
  close(fd_client);
  unlink(path_listen);
}

TEST(StatsDTest, Format) {
  // Note that the result is hardcoded, based on what the spec says it should
  // be; we don't bring in any kind of statsd validation lib or compare types
  const char path_listen[] = "/tmp/my_statsd_listener";
  const char answer[] = "foo:9999|g";
  unlink(path_listen); // Make sure the default listening path is available

  int fd_listener;
  DDRes lres = statsd_listen(path_listen, strlen(path_listen), &fd_listener);
  int fd_client;
  DDRes cres = statsd_connect(path_listen, strlen(path_listen), &fd_client);

  // This should pass if the previous test passed, but we check anyway
  EXPECT_TRUE(IsDDResOK(lres));
  EXPECT_TRUE(IsDDResOK(cres));

  // Now try sending
  long value = 9999;
  DDRes sres = statsd_send(fd_client, "foo", &value, STAT_GAUGE);
  EXPECT_TRUE(IsDDResOK(sres));

  // In order to valid, we have to use lower-level socket interfaces, since
  // obviously we have no need to imeplement a statsd server :)
  char buf[1024] = {0};
  EXPECT_TRUE(1 <= read(fd_listener, buf, 1024));
  EXPECT_EQ(0, strcmp(buf, answer));

  // Cleanup
  close(fd_listener);
  close(fd_client);
  unlink(path_listen);
}
