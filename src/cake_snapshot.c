/* SPDX-License-Identifier: GPL-2.0-or-later
 * One read-only rtnetlink dump, exact software CAKE roots only. No tc/NSS
 * formatting plugins, shell rate parsing, configuration or hardware writes.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/netlink.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_ROOTS 64
#define BYTE_LIMIT (1024U * 1024U)
#define SAMPLE_MS 750
struct root_rate {
  int ifindex;
  unsigned int seen;
  uint64_t bytes;
};
struct sample {
  struct root_rate roots[MAX_ROOTS];
  int count, done, failed;
  uint32_t sequence;
};

static int attributes(const unsigned char *p, size_t len,
                      const struct rtattr **kind,
                      const struct rtattr **options) {
  while (len) {
    struct rtattr h;
    size_t step;
    unsigned int type;
    if (len < sizeof(h))
      return -1;
    memcpy(&h, p, sizeof(h));
    if (h.rta_len < sizeof(h) || h.rta_len > len)
      return -1;
    step = RTA_ALIGN(h.rta_len);
    if (step > len)
      return -1;
    type = h.rta_type & NLA_TYPE_MASK;
    if (type == TCA_KIND) {
      if (*kind)
        return -1;
      *kind = (const struct rtattr *)p;
    }
    if (type == TCA_OPTIONS) {
      if (*options)
        return -1;
      *options = (const struct rtattr *)p;
    }
    p += step;
    len -= step;
  }
  return 0;
}
static int base_rate(const struct rtattr *options, uint64_t *rate) {
  const unsigned char *p = RTA_DATA(options);
  size_t len = RTA_PAYLOAD(options);
  int found = 0;
  while (len) {
    struct rtattr h;
    size_t step;
    if (len < sizeof(h))
      return -1;
    memcpy(&h, p, sizeof(h));
    if (h.rta_len < sizeof(h) || h.rta_len > len)
      return -1;
    step = RTA_ALIGN(h.rta_len);
    if (step > len)
      return -1;
    if ((h.rta_type & NLA_TYPE_MASK) == TCA_CAKE_BASE_RATE64) {
      if (found || h.rta_len != RTA_LENGTH(sizeof(*rate)))
        return -1;
      memcpy(rate, p + RTA_LENGTH(0), sizeof(*rate));
      found = 1;
    }
    p += step;
    len -= step;
  }
  /* Zero means unlimited; cap only to keep exact bit-rate conversion in u64. */
  return found && *rate > 0 && *rate <= UINT64_MAX / 8 ? 0 : -1;
}
static int qdisc(struct sample *s, const struct nlmsghdr *nh) {
  struct tcmsg t;
  const struct rtattr *kind = NULL, *options = NULL;
  int index = -1;
  if (nh->nlmsg_len < NLMSG_LENGTH(sizeof(t)))
    return -1;
  memcpy(&t, NLMSG_DATA(nh), sizeof(t));
  if (t.tcm_parent != TC_H_ROOT)
    return 0;
  for (int i = 0; i < s->count; i++)
    if (s->roots[i].ifindex == t.tcm_ifindex)
      index = i;
  if (index < 0)
    return 0; /* Never interpret unrelated NSS/plugin attributes. */
  if (s->roots[index].seen)
    return -1;
  if (attributes((const unsigned char *)NLMSG_DATA(nh) + NLMSG_ALIGN(sizeof(t)),
                 nh->nlmsg_len - NLMSG_LENGTH(sizeof(t)), &kind, &options))
    return -1;
  if (!kind || RTA_PAYLOAD(kind) != 5 || memcmp(RTA_DATA(kind), "cake", 5) ||
      !options)
    return -1;
  if (base_rate(options, &s->roots[index].bytes))
    return -1;
  s->roots[index].seen = 1;
  return 0;
}
static int parse_datagram(struct sample *s, const unsigned char *p,
                          size_t len) {
  while (len) {
    struct nlmsghdr h;
    size_t step;
    if (len < sizeof(h))
      return -1;
    memcpy(&h, p, sizeof(h));
    if (h.nlmsg_len < sizeof(h) || h.nlmsg_len > len)
      return -1;
    step = NLMSG_ALIGN(h.nlmsg_len);
    if (step > len)
      return -1;
    if (s->done || h.nlmsg_seq != s->sequence ||
        (h.nlmsg_flags & NLM_F_DUMP_INTR))
      return -1;
    if (h.nlmsg_type == NLMSG_DONE) {
      int error = 0;
      size_t body = h.nlmsg_len - NLMSG_HDRLEN;
      if (body && body < sizeof(error))
        return -1;
      if (body)
        memcpy(&error, p + NLMSG_HDRLEN, sizeof(error));
      if (error)
        return -1;
      s->done = 1;
    } else if (h.nlmsg_type == RTM_NEWQDISC) {
      if (!(h.nlmsg_flags & NLM_F_MULTI) ||
          qdisc(s, (const struct nlmsghdr *)p))
        return -1;
    } else
      return -1; /* Includes NLMSG_ERROR/OVERRUN; no partial success. */
    p += step;
    len -= step;
  }
  return 0;
}
static int datagram(struct sample *s, const unsigned char *p, size_t len) {
  if (s->failed || !len || parse_datagram(s, p, len)) {
    s->failed = 1;
    return -1;
  }
  return 0;
}
static int complete(const struct sample *s) {
  if (s->failed || !s->done || s->count < 1 || s->count > MAX_ROOTS)
    return 0;
  for (int i = 0; i < s->count; i++)
    if (s->roots[i].seen != 1)
      return 0;
  return 1;
}
static int64_t monotonic_ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    return -1;
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
/* Resolve by interface name in the current network namespace; no sysfs mount
 * needed. */
static int indices(struct sample *s, int count, char **names) {
  for (int n = 0; n < count; n++) {
    unsigned int value = if_nametoindex(names[n]);
    if (!value || value > INT_MAX)
      return -1;
    for (int i = 0; i < s->count; i++)
      if (s->roots[i].ifindex == (int)value)
        return -1;
    s->roots[s->count++].ifindex = (int)value;
  }
  return 0;
}
static int valid_name(const char *name) {
  size_t len = strlen(name);
  if (!len || len >= IF_NAMESIZE || !strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  for (size_t i = 0; i < len; i++)
    if ((unsigned char)name[i] < 33 || (unsigned char)name[i] > 126 ||
        name[i] == '/' || name[i] == ':')
      return 0;
  return 1;
}
static void json_name(const char *name) {
  putchar('"');
  for (; *name; name++) {
    if (*name == '"' || *name == '\\')
      putchar('\\');
    putchar(*name);
  }
  putchar('"');
}
static void usage(FILE *f) {
  fputs(
      "Usage: cake-snapshot [--timeout-ms 1..60000] [--] IFACE [IFACE ...]\n"
      "Read 1..64 finite-rate root CAKE qdiscs in one read-only netlink dump.\n"
      "All requested interfaces must succeed; errors leave stdout empty.\n",
      f);
}
static int receive_sample(struct sample *s, int fd, int64_t deadline) {
  size_t bytes = 0;
  _Alignas(8) unsigned char buffer[65536];
  while (!s->done) {
    int64_t at = monotonic_ms();
    if (at < 0 || at >= deadline)
      return -1;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int rc = poll(&pfd, 1, (int)(deadline - at));
    if (rc < 0 && errno == EINTR)
      continue;
    if (rc <= 0 || pfd.revents != POLLIN)
      return -1;
    struct sockaddr_nl peer = {0};
    struct iovec iov = {.iov_base = buffer, .iov_len = sizeof(buffer)};
    struct msghdr msg = {.msg_name = &peer,
                         .msg_namelen = sizeof(peer),
                         .msg_iov = &iov,
                         .msg_iovlen = 1};
    ssize_t got = recvmsg(fd, &msg, MSG_DONTWAIT);
    if (got < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (got <= 0 || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
        msg.msg_namelen < sizeof(peer) || peer.nl_family != AF_NETLINK ||
        peer.nl_pid != 0)
      return -1;
    bytes += (size_t)got;
    if (bytes > BYTE_LIMIT || datagram(s, buffer, (size_t)got))
      return -1;
  }
  int64_t finished = monotonic_ms();
  return complete(s) && finished >= 0 && finished < deadline ? 0 : -1;
}
int main(int argc, char **argv) {
  struct sample s = {0};
  int count, first = 1, timeout = SAMPLE_MS;
  int fd = -1, rc = EXIT_FAILURE;
  int64_t began = monotonic_ms();
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    usage(stdout);
    return 0;
  }
  if (first < argc && !strcmp(argv[first], "--timeout-ms")) {
    char *end;
    long value;
    if (argc <= first + 1 || !argv[first + 1][0]) {
      usage(stderr);
      return 2;
    }
    errno = 0;
    value = strtol(argv[first + 1], &end, 10);
    if (errno || *end || value < 1 || value > 60000) {
      usage(stderr);
      return 2;
    }
    timeout = (int)value;
    first += 2;
  }
  if (first < argc && !strcmp(argv[first], "--"))
    first++;
  count = argc - first;
  if (count < 1 || count > MAX_ROOTS) {
    usage(stderr);
    return 2;
  }
  for (int i = first; i < argc; i++)
    if (!valid_name(argv[i]) ||
        (i == first && argv[i][0] == '-' &&
         (first < 2 || strcmp(argv[first - 1], "--")))) {
      usage(stderr);
      return 2;
    }
  if (began < 0 || indices(&s, count, argv + first))
    goto out;
  s.sequence = (uint32_t)getpid();
  if (!s.sequence)
    goto out;
  fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
              NETLINK_ROUTE);
  if (fd < 0)
    goto out;
  struct sockaddr_nl local = {.nl_family = AF_NETLINK},
                     kernel = {.nl_family = AF_NETLINK};
  int rcvbuf = 262144;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) ||
      bind(fd, (struct sockaddr *)&local, sizeof(local)))
    goto out;
  struct {
    struct nlmsghdr n;
    struct tcmsg t;
  } request = {.n = {.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
                     .nlmsg_type = RTM_GETQDISC,
                     .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP},
               .t = {.tcm_family = AF_UNSPEC}};
  request.n.nlmsg_seq = s.sequence;
  if (sendto(fd, &request, request.n.nlmsg_len, 0, (struct sockaddr *)&kernel,
             sizeof(kernel)) != (ssize_t)request.n.nlmsg_len)
    goto out;
  if (receive_sample(&s, fd, began + timeout))
    goto out;
  /* Recheck netdev identity after the dump, before publishing any rate. */
  struct sample current = {0};
  if (indices(&current, count, argv + first))
    goto out;
  for (int i = 0; i < s.count; i++)
    if (current.roots[i].ifindex != s.roots[i].ifindex)
      goto out;
  int64_t finished = monotonic_ms();
  if (finished < 0 || finished >= began + timeout)
    goto out;
  printf("{\"schema_version\":1,\"duration_ms\":%" PRId64 ",\"rates\":[",
         finished - began);
  for (int i = 0; i < s.count; i++) {
    printf("%s{\"dev\":", i ? "," : "");
    json_name(argv[first + i]);
    printf(",\"ifindex\":%d,\"bytes_per_second\":\"%" PRIu64
           "\",\"bits_per_second\":\"%" PRIu64 "\"}",
           s.roots[i].ifindex, s.roots[i].bytes, s.roots[i].bytes * 8);
  }
  puts("]}");
  rc = ferror(stdout) ? EXIT_FAILURE : EXIT_SUCCESS;
out:
  if (fd >= 0)
    close(fd);
  if (rc != EXIT_SUCCESS)
    fputs("CAKE read-only snapshot incomplete or expired\n", stderr);
  return rc;
}
