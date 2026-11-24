#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/sys.h>
#include <lwip/timeouts.h>
#include <lwip/udp.h>
#include <string.h>
#include <time.h>

static const u8_t first[] = {0x41, 0x00, 0xfe};
static const u8_t second[] = {0x52, 0xaa, 0x79, 0x00, 0x17};
static ip_addr_t  loopback;
static unsigned   received, invalid, expired;

static void
receive(void *arg, struct udp_pcb *pcb, struct pbuf *packet, const ip_addr_t *source, u16_t port)
{
  const u8_t *expected = received ? second : first;
  u16_t       length = received ? sizeof(second) : sizeof(first);
  u8_t        bytes[sizeof(second)];
  (void) arg;
  (void) pcb;
  if (received >= 2 || !ip_addr_cmp(source, &loopback) || port != 18101 ||
      packet->tot_len != length || pbuf_copy_partial(packet, bytes, length, 0) != length ||
      memcmp(bytes, expected, length))
    invalid = 1;
  received++;
  pbuf_free(packet);
}

static int
sendpacket(struct udp_pcb *sender, const u8_t *data, u16_t length)
{
  struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
  err_t        error;
  if (!packet)
    return 0;
  error = pbuf_take(packet, data, length);
  if (error == ERR_OK)
    error = udp_sendto(sender, packet, &loopback, 18100);
  pbuf_free(packet);
  return error == ERR_OK;
}

static void
timeout(void *arg)
{
  if (arg == &expired)
    expired++;
}

int
main(void)
{
  struct udp_pcb       *sender, *receiver;
  const struct timespec tick = {0, 10000000L};
  u32_t                 start;
  unsigned              i;
  lwip_init();
  IP_ADDR4(&loopback, 127, 0, 0, 1);
  sender = udp_new();
  receiver = udp_new();
  if (!sender || !receiver || udp_bind(sender, &loopback, 18101) != ERR_OK ||
      udp_bind(receiver, &loopback, 18100) != ERR_OK)
    return 61;
  udp_recv(receiver, receive, NULL);
  if (!sendpacket(sender, first, sizeof(first)) || !sendpacket(sender, second, sizeof(second)) ||
      received)
    return 62;
  netif_poll_all();
  udp_remove(sender);
  udp_remove(receiver);
  if (received != 2 || invalid)
    return 63;
  start = sys_now();
  sys_timeout(20, timeout, &expired);
  for (i = 0; i < 32 && !expired; i++) {
    if (nanosleep(&tick, NULL))
      return 64;
    sys_check_timeouts();
  }
  if (expired != 1 || (u32_t) (sys_now() - start) < 20)
    return 65;
  sys_check_timeouts();
  return expired == 1 ? 42 : 66;
}
