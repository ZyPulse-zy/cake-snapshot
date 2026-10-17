/* Actual binary decoder, synthetic kernel messages; no live sockets or writes.
 */
#define main production_main
#include "../src/cake_snapshot.c"
#undef main
#include <assert.h>
static unsigned int checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    assert(x);                                                                 \
  } while (0)
static void attr(unsigned char *p, size_t *at, unsigned int type,
                 const void *data, size_t size) {
  struct rtattr h = {.rta_type = (unsigned short)type,
                     .rta_len = (unsigned short)RTA_LENGTH(size)};
  memcpy(p + *at, &h, sizeof(h));
  if (size)
    memcpy(p + *at + sizeof(h), data, size);
  *at += RTA_ALIGN(h.rta_len);
}
static size_t frame(unsigned char *p, int index, const char *kind,
                    uint64_t rate, int duplicate) {
  memset(p, 0, 512);
  struct nlmsghdr *h = (struct nlmsghdr *)p;
  struct tcmsg *t = (struct tcmsg *)NLMSG_DATA(h);
  h->nlmsg_seq = 7;
  h->nlmsg_type = RTM_NEWQDISC;
  h->nlmsg_flags = NLM_F_MULTI;
  t->tcm_ifindex = index;
  t->tcm_parent = TC_H_ROOT;
  size_t at = NLMSG_LENGTH(sizeof(*t));
  attr(p, &at, TCA_KIND, kind, strlen(kind) + 1);
  size_t opts = at;
  attr(p, &at, TCA_OPTIONS, NULL, 0);
  attr(p, &at, TCA_CAKE_BASE_RATE64, &rate, sizeof(rate));
  if (duplicate)
    attr(p, &at, TCA_CAKE_BASE_RATE64, &rate, sizeof(rate));
  ((struct rtattr *)(p + opts))->rta_len = (unsigned short)(at - opts);
  h->nlmsg_len = (unsigned int)at;
  return at;
}
static void setup(struct sample *s, int count) {
  memset(s, 0, sizeof(*s));
  s->sequence = 7;
  s->count = count;
  for (int i = 0; i < count; i++)
    s->roots[i].ifindex = 100 + i;
}
static int done(struct sample *s, int error) {
  struct {
    struct nlmsghdr h;
    int value;
  } msg = {.h = {.nlmsg_len = NLMSG_LENGTH(sizeof(int)),
                 .nlmsg_type = NLMSG_DONE,
                 .nlmsg_seq = 7},
           .value = error};
  return datagram(s, (const unsigned char *)&msg, sizeof(msg));
}
int main(void) {
  _Alignas(8) unsigned char p[512], mutated[512];
  struct sample s;
  size_t len;
  for (int count = 1; count <= MAX_ROOTS; count++) {
    setup(&s, count);
    for (int i = 0; i < count; i++) {
      len = frame(p, 100 + i, "cake", 4055000 + (uint64_t)i, 0);
      CHECK(datagram(&s, p, len) == 0);
      CHECK(!complete(&s));
    }
    CHECK(done(&s, 0) == 0);
    CHECK(complete(&s));
    CHECK(s.roots[0].bytes == 4055000);
    CHECK(datagram(&s, p, len) != 0);
  }
  setup(&s, 12);
  len = frame(p, 999, "unsupported-nss-kind", 0, 0);
  /* An unrelated firmware qdisc must not become a software root or invoke a
   * decoder. */
  CHECK(datagram(&s, p, len) == 0);
  CHECK(s.roots[0].seen == 0);
  setup(&s, 12);
  len = frame(p, 100, "cake", 5000000, 0);
  CHECK(datagram(&s, p, len) == 0);
  CHECK(datagram(&s, p, len) != 0);
  setup(&s, 12);
  CHECK(done(&s, 0) == 0);
  CHECK(!complete(&s));
  setup(&s, 12);
  CHECK(done(&s, -EINTR) != 0);
  for (int test = 0; test < 10; test++) {
    setup(&s, 12);
    len = frame(p, 100, test == 0 ? "htb" : "cake",
                test == 1 ? 0 : (test == 2 ? UINT64_MAX : 5000000), test == 3);
    struct nlmsghdr *h = (struct nlmsghdr *)p;
    if (test == 4)
      h->nlmsg_flags |= NLM_F_DUMP_INTR;
    if (test == 5)
      h->nlmsg_seq++;
    if (test == 6)
      h->nlmsg_type = NLMSG_ERROR;
    if (test == 7)
      h->nlmsg_len = 1;
    if (test == 8)
      ((struct rtattr *)(p + NLMSG_LENGTH(sizeof(struct tcmsg))))->rta_len =
          65535;
    if (test == 9)
      len--;
    CHECK(datagram(&s, p, len) != 0);
    CHECK(!complete(&s));
  }
  /* Every truncated prefix and every single-byte mutation must remain memory
   * safe. */
  len = frame(p, 100, "cake", 5000000, 0);
  for (size_t length = 1; length < len; length++) {
    setup(&s, 12);
    (void)datagram(&s, p, length);
    CHECK(!complete(&s));
  }
  for (size_t byte = 0; byte < len; byte++) {
    memcpy(mutated, p, len);
    mutated[byte] ^= 255;
    setup(&s, 12);
    (void)datagram(&s, mutated, len);
    CHECK(!complete(&s));
  }
  /* General link rates: sub-Mbit, >200Mbit and maximal exact conversion. */
  for (unsigned int i = 0; i < 4; i++) {
    const uint64_t rates[] = {1, 12500, 1250000000, UINT64_MAX / 8};
    setup(&s, 1);
    len = frame(p, 100, "cake", rates[i], 0);
    CHECK(datagram(&s, p, len) == 0);
    CHECK(done(&s, 0) == 0);
    CHECK(complete(&s));
    CHECK(s.roots[0].bytes == rates[i]);
  }
  setup(&s, 1);
  CHECK(datagram(&s, p, 0) != 0);
  CHECK(!complete(&s));
  CHECK(valid_name("pppoe-wan"));
  CHECK(valid_name("vlan.123"));
  CHECK(!valid_name(""));
  CHECK(!valid_name("abcdefghijklmnop"));
  CHECK(!valid_name("bad/name"));
  CHECK(!valid_name("bad name"));
  CHECK(!valid_name(".."));
  CHECK(!valid_name("bad\nname"));
  CHECK(RTM_GETQDISC != RTM_NEWQDISC && RTM_GETQDISC != RTM_DELQDISC);
  printf("{\"passed\":true,\"checks\":%u,\"syntheticNetlinkOnly\":true,"
         "\"liveSocketOpened\":false}\n",
         checks);
  return 0;
}
