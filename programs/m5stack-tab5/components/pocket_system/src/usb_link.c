/*
 * The Pocket Link over the USB-C cable. The Tab5's USB-C port is the
 * ESP32-P4's USB Serial/JTAG controller, one CDC-ACM serial line that also
 * carries the console log. A companion on the other end of the cable (a
 * phone in USB host mode, or a computer) speaks framed packets on that line;
 * the log stays plain text between them.
 *
 * The link client (engine/host-services link.c) only knows TCP, so this
 * module gives it one on the loopback interface: a listener on
 * 127.0.0.1:8628 whose accepted connection is carried over the cable as
 * OPEN, DATA and CLOSE packets. A companion pairs a console over the cable
 * by sending a pairing URI whose host is 127.0.0.1 (PAIR), and keeps the
 * link client finding it by sending its beacon, which this module delivers
 * to the link client's beacon socket as a datagram from 127.0.0.1 (BEACON).
 * While a companion answers on the cable, link_host_net_ready() holds
 * without Wi-Fi.
 *
 * Packet: F5 50 type len16 payload crc16, little endian; the CRC is
 * CRC-16/CCITT-FALSE over type, length and payload. 0xF5 never occurs in
 * UTF-8 text, so a reader takes every byte outside a valid packet as log.
 *
 *   console -> companion  HELLO  link.status() JSON, once a second
 *                         OPEN   conn u8: the link client connected
 *                         DATA   conn u8, bytes
 *                         CLOSE  conn u8
 *   companion -> console  ATTACH (empty) once a second: a companion is here
 *                         DATA, CLOSE as above
 *                         PAIR   a pocketlink://pair URI, as if scanned
 *                         BEACON a 64-byte Pocket Link beacon
 */

#include <errno.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "link.h"
#include "lwip/sockets.h"
#include "system_internal.h"

#define USB_LINK_PORT 8628
#define USB_BEACON_PORT 8627
#define USB_MAX_PAYLOAD 2048
#define USB_ATTACH_TIMEOUT_US (3 * 1000 * 1000)
#define USB_RX_BUFFER 16384
#define USB_TX_BUFFER 16384

enum {
  PKT_HELLO = 0x01,
  PKT_ATTACH = 0x02,
  PKT_OPEN = 0x10,
  PKT_DATA = 0x11,
  PKT_CLOSE = 0x12,
  PKT_PAIR = 0x20,
  PKT_BEACON = 0x21,
};

static const char *TAG = "usb_link";
static SemaphoreHandle_t tx_lock;
static _Atomic int64_t attached_at;
static _Atomic int client_fd = -1;
static _Atomic uint8_t conn_id;
static uint8_t tx_packet[USB_MAX_PAYLOAD + 7];

static uint16_t crc16(const uint8_t *bytes, size_t length, uint16_t crc) {
  for (size_t i = 0; i < length; i++) {
    crc ^= (uint16_t)bytes[i] << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

/* One packet in one driver write, so log lines written meanwhile land
 * before or after it, never inside. */
static bool send_packet(uint8_t type, uint8_t conn, bool with_conn,
                        const void *payload, size_t length) {
  size_t body = length + (with_conn ? 1 : 0);
  if (body > USB_MAX_PAYLOAD)
    return false;
  xSemaphoreTake(tx_lock, portMAX_DELAY);
  uint8_t *p = tx_packet;
  p[0] = 0xF5;
  p[1] = 0x50;
  p[2] = type;
  p[3] = (uint8_t)(body & 0xff);
  p[4] = (uint8_t)(body >> 8);
  size_t at = 5;
  if (with_conn)
    p[at++] = conn;
  if (length > 0)
    memcpy(p + at, payload, length);
  at += length;
  uint16_t crc = crc16(p + 2, 3 + body, 0xFFFF);
  p[at++] = (uint8_t)(crc & 0xff);
  p[at++] = (uint8_t)(crc >> 8);
  size_t sent = 0;
  while (sent < at) {
    int n =
        usb_serial_jtag_write_bytes(p + sent, at - sent, pdMS_TO_TICKS(200));
    if (n <= 0)
      break;
    sent += (size_t)n;
  }
  xSemaphoreGive(tx_lock);
  return sent == at;
}

bool usb_link_attached(void) {
  int64_t at = atomic_load(&attached_at);
  return at != 0 && esp_timer_get_time() - at < USB_ATTACH_TIMEOUT_US;
}

static void close_client(bool tell) {
  int fd = atomic_exchange(&client_fd, -1);
  if (fd >= 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
    if (tell)
      send_packet(PKT_CLOSE, atomic_load(&conn_id), true, NULL, 0);
  }
}

// ---------------------------------------------------------------------------
// The companion's packets
// ---------------------------------------------------------------------------

typedef struct {
  const char *uri;
  size_t length;
  bool ok;
} PairJob;

static void pair_job(void *argument) {
  PairJob *job = argument;
  job->ok = link_pair(job->uri, job->length);
}

static void deliver_beacon(const uint8_t *bytes, size_t length) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0)
    return;
  struct sockaddr_in from = {.sin_family = AF_INET,
                             .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                             .sin_port = 0};
  struct sockaddr_in to = {.sin_family = AF_INET,
                           .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                           .sin_port = htons(USB_BEACON_PORT)};
  bind(fd, (struct sockaddr *)&from, sizeof from);
  sendto(fd, bytes, length, 0, (struct sockaddr *)&to, sizeof to);
  close(fd);
}

static void packet(uint8_t type, const uint8_t *payload, size_t length) {
  atomic_store(&attached_at, esp_timer_get_time());
  switch (type) {
  case PKT_ATTACH:
    break;
  case PKT_DATA: {
    if (length < 1 || payload[0] != atomic_load(&conn_id))
      break;
    int fd = atomic_load(&client_fd);
    size_t at = 1;
    while (fd >= 0 && at < length) {
      ssize_t n = send(fd, payload + at, length - at, 0);
      if (n <= 0) {
        close_client(true);
        break;
      }
      at += (size_t)n;
    }
    break;
  }
  case PKT_CLOSE:
    if (length >= 1 && payload[0] == atomic_load(&conn_id))
      close_client(false);
    break;
  case PKT_PAIR: {
    PairJob job = {.uri = (const char *)payload, .length = length};
    system_run(pair_job, &job);
    ESP_LOGI(TAG, "pairing over the cable: %s", job.ok ? "started" : "refused");
    break;
  }
  case PKT_BEACON:
    deliver_beacon(payload, length);
    break;
  default:
    break;
  }
}

static void rx_task(void *unused) {
  (void)unused;
  static uint8_t chunk[512];
  static uint8_t body[USB_MAX_PAYLOAD + 2];
  enum { WAIT_F5, WAIT_50, TYPE, LEN0, LEN1, BODY } state = WAIT_F5;
  uint8_t type = 0;
  size_t length = 0, got = 0;
  for (;;) {
    int n = usb_serial_jtag_read_bytes(chunk, sizeof chunk, pdMS_TO_TICKS(100));
    for (int i = 0; i < n; i++) {
      uint8_t b = chunk[i];
      switch (state) {
      case WAIT_F5:
        if (b == 0xF5)
          state = WAIT_50;
        break;
      case WAIT_50:
        state = b == 0x50 ? TYPE : (b == 0xF5 ? WAIT_50 : WAIT_F5);
        break;
      case TYPE:
        type = b;
        state = LEN0;
        break;
      case LEN0:
        length = b;
        state = LEN1;
        break;
      case LEN1:
        length |= (size_t)b << 8;
        got = 0;
        state = length > USB_MAX_PAYLOAD ? WAIT_F5 : BODY;
        break;
      case BODY:
        body[got++] = b;
        if (got == length + 2) {
          uint8_t head[3] = {type, (uint8_t)(length & 0xff),
                             (uint8_t)(length >> 8)};
          uint16_t crc = crc16(body, length, crc16(head, 3, 0xFFFF));
          if ((uint16_t)(body[length] | (body[length + 1] << 8)) == crc)
            packet(type, body, length);
          state = WAIT_F5;
        }
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The loopback listener the link client connects to
// ---------------------------------------------------------------------------

static void relay_task(void *unused) {
  (void)unused;
  static uint8_t buffer[USB_MAX_PAYLOAD - 1];
  int listener = -1;
  for (;;) {
    if (listener < 0) {
      listener = socket(AF_INET, SOCK_STREAM, 0);
      int reuse = 1;
      setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
      struct sockaddr_in address = {.sin_family = AF_INET,
                                    .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                                    .sin_port = htons(USB_LINK_PORT)};
      if (listener < 0 ||
          bind(listener, (struct sockaddr *)&address, sizeof address) != 0 ||
          listen(listener, 1) != 0) {
        ESP_LOGW(TAG, "loopback listener: errno %d", errno);
        if (listener >= 0)
          close(listener);
        listener = -1;
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!usb_link_attached()) {
      /* no companion on the cable: the link client tries another address */
      close(fd);
      continue;
    }
    close_client(true);
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);
    uint8_t id = (uint8_t)(atomic_load(&conn_id) + 1);
    atomic_store(&conn_id, id);
    atomic_store(&client_fd, fd);
    send_packet(PKT_OPEN, id, true, NULL, 0);
    for (;;) {
      ssize_t n = recv(fd, buffer, sizeof buffer, 0);
      if (n <= 0 || atomic_load(&client_fd) != fd)
        break;
      if (!send_packet(PKT_DATA, id, true, buffer, (size_t)n))
        break;
    }
    if (atomic_load(&client_fd) == fd)
      close_client(true);
  }
}

typedef struct {
  char json[USB_MAX_PAYLOAD];
  size_t length;
} StatusJob;

static void status_job(void *argument) {
  StatusJob *job = argument;
  const char *json = link_status_json();
  job->length = json ? strnlen(json, sizeof job->json) : 0;
  memcpy(job->json, json ? json : "", job->length);
}

static void hello_task(void *unused) {
  (void)unused;
  static StatusJob job;
  for (;;) {
    system_run(status_job, &job);
    send_packet(PKT_HELLO, 0, false, job.json, job.length);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void usb_link_start(void) {
  if (tx_lock != NULL)
    return;
  /* The loopback listener needs lwIP's thread, which the radio would start
   * later or not at all; esp_netif_init returns at once once it runs. */
  esp_err_t netif = esp_netif_init();
  if (netif != ESP_OK) {
    ESP_LOGW(TAG, "esp_netif_init: %s", esp_err_to_name(netif));
    return;
  }
  tx_lock = xSemaphoreCreateMutex();
  usb_serial_jtag_driver_config_t config = {.rx_buffer_size = USB_RX_BUFFER,
                                            .tx_buffer_size = USB_TX_BUFFER};
  esp_err_t err = usb_serial_jtag_driver_install(&config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "USB Serial/JTAG driver: %s", esp_err_to_name(err));
    return;
  }
  /* the console writes through the driver too, a whole write at a time */
  usb_serial_jtag_vfs_use_driver();
  xTaskCreatePinnedToCore(rx_task, "usb_link_rx", 4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(relay_task, "usb_link", 4096, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(hello_task, "usb_hello", 3072, NULL, 2, NULL, 1);
  ESP_LOGI(TAG, "Pocket Link over USB-C on 127.0.0.1:%d", USB_LINK_PORT);
}
