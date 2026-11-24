#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/sys.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <lwip/udp.h>
#include <string.h>
#include <time.h>

static const u8_t first[] = {0x41, 0x00, 0xfe};
static const u8_t second[] = {0x52, 0xaa, 0x79, 0x00, 0x17};
static ip_addr_t  loopback;
static unsigned   received, invalid, expired;

#define TCP_PAYLOAD (2 * TCP_MSS + 71)
static u8_t     stream[TCP_PAYLOAD];
static unsigned connected, accepted, streambytes, streamreads, serverfin, clientfin, responsebytes;

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

static void
tcperror(void *arg, err_t error)
{
  (void) arg;
  (void) error;
  invalid = 1;
}

static err_t
readstream(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t error)
{
  const u8_t *expected = arg == stream ? stream : second;
  unsigned   *offset = arg == stream ? &streambytes : &responsebytes;
  unsigned    length = arg == stream ? sizeof(stream) : sizeof(second);
  u16_t       i;
  if (error != ERR_OK)
    invalid = 1;
  if (packet) {
    if (arg == stream)
      streamreads++;
    if (*offset > length || packet->tot_len > length - *offset)
      invalid = 1;
    else {
      for (i = 0; i < packet->tot_len; i++)
        if (pbuf_get_at(packet, i) != expected[*offset + i])
          invalid = 1;
      *offset += packet->tot_len;
    }
    tcp_recved(pcb, packet->tot_len);
    pbuf_free(packet);
  } else if (arg == stream) {
    serverfin++;
    if (*offset != length ||
        tcp_write(pcb, second, sizeof(second), TCP_WRITE_FLAG_COPY) != ERR_OK ||
        tcp_close(pcb) != ERR_OK)
      invalid = 1;
  } else {
    clientfin++;
    if (*offset != length)
      invalid = 1;
  }
  return ERR_OK;
}

static err_t
acceptstream(void *arg, struct tcp_pcb *pcb, err_t error)
{
  (void) arg;
  if (error != ERR_OK || !pcb || accepted) {
    invalid = 1;
    return ERR_VAL;
  }
  accepted++;
  tcp_arg(pcb, stream);
  tcp_err(pcb, tcperror);
  tcp_recv(pcb, readstream);
  return ERR_OK;
}

static err_t
connectstream(void *arg, struct tcp_pcb *pcb, err_t error)
{
  (void) arg;
  (void) pcb;
  if (error == ERR_OK)
    connected++;
  else
    invalid = 1;
  return ERR_OK;
}

static int
tcpcheck(const struct timespec *tick)
{
  struct tcp_pcb *listener = tcp_new(), *client = tcp_new();
  unsigned        i;
  if (!listener || !client || tcp_bind(listener, &loopback, 18102) != ERR_OK)
    return 67;
  listener = tcp_listen_with_backlog(listener, 1);
  if (!listener)
    return 68;
  tcp_accept(listener, acceptstream);
  tcp_err(client, tcperror);
  tcp_recv(client, readstream);
  if (tcp_connect(client, &loopback, 18102, connectstream) != ERR_OK || connected || accepted)
    return 69;
  for (i = 0; i < 32 && (!connected || !accepted) && !invalid; i++) {
    netif_poll_all();
    sys_check_timeouts();
    if ((!connected || !accepted) && nanosleep(tick, NULL))
      return 70;
  }
  if (invalid || connected != 1 || accepted != 1)
    return 71;
  for (i = 0; i < sizeof(stream); i++)
    stream[i] = (u8_t) (i * 37 + i / 256);
  if (tcp_write(client, stream, sizeof(stream), TCP_WRITE_FLAG_COPY) != ERR_OK ||
      tcp_shutdown(client, 0, 1) != ERR_OK || serverfin || clientfin)
    return 72;
  for (i = 0; i < 64 && !clientfin && !invalid; i++) {
    netif_poll_all();
    sys_check_timeouts();
    if (!clientfin && nanosleep(tick, NULL))
      return 73;
  }
  netif_poll_all();
  if (invalid || serverfin != 1 || clientfin != 1 || streambytes != sizeof(stream) ||
      streamreads < 2 || responsebytes != sizeof(second) || tcp_close(listener) != ERR_OK)
    return 74;
  return 42;
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
  return expired == 1 ? tcpcheck(&tick) : 66;
}
