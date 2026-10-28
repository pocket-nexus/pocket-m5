#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5/tab5.h"
#include "pocket/io_esp.h"
#include "pocket/worker_esp.h"
#include "pocket/worker_ui.h"

extern const uint8_t
    worker_source_start[] asm("_binary_worker_io_worker_js_start");
extern const uint8_t worker_source_end[] asm("_binary_worker_io_worker_js_end");

void app_main(void) {
  ESP_ERROR_CHECK(m5_tab5_init());
  ESP_LOGI("worker_test", "panel=%s", m5_tab5_panel_name(m5_tab5_panel()));
  /* Only these two devices are application-owned. Touch, expanders, codecs
   * and power monitoring remain inaccessible to raw worker IO. */
  const uint8_t addresses[] = {0x32, 0x68};
  const pocket_i2c_esp_config i2c_config = {.struct_size = sizeof(i2c_config),
                                            .bus = m5_tab5_i2c(),
                                            .addresses = addresses,
                                            .address_count = 2,
                                            .speed_hz = 400000,
                                            .max_transaction_bytes = 256,
                                            .max_timeout_ms = 20,
                                            .task_core = 1};
  pocket_i2c_esp *i2c = pocket_i2c_esp_create(&i2c_config);
  /* Internal loopback exercises real UART FIFO/interrupt/RX transport without
   * requiring a wire. GPIO53/54 are Tab5 PORT.A; its 5 V rail remains off. */
  const pocket_uart_esp_config uart_config = {.struct_size =
                                                  sizeof(uart_config),
                                              .port = UART_NUM_1,
                                              .tx_pin = 53,
                                              .rx_pin = 54,
                                              .rts_pin = -1,
                                              .cts_pin = -1,
                                              .max_baud = 115200,
                                              .loopback = true,
                                              .task_core = 1};
  pocket_uart_esp *uart = pocket_uart_esp_create(&uart_config);
  if (!i2c || !uart) {
    ESP_LOGE("worker_test", "driver creation failed");
    return;
  }
  const pocket_worker_node nodes[] = {
      {.struct_size = sizeof(pocket_worker_node),
       .kind = POCKET_WORKER_I2C,
       .name = "i2c0",
       .driver = pocket_i2c_esp_driver(i2c)},
      {.struct_size = sizeof(pocket_worker_node),
       .kind = POCKET_WORKER_UART,
       .name = "uart1",
       .driver = pocket_uart_esp_driver(uart)},
  };
  /* This diagnostic realm represents independent UI work. It has its own
   * runtime, lives on CPU0, and executes at 50 Hz while worker IO uses CPU1. */
  JSRuntime *ui_runtime = JS_NewRuntime();
  JSContext *ui = ui_runtime ? JS_NewContext(ui_runtime) : NULL;
  if (!ui) {
    ESP_LOGE("worker_test", "UI diagnostic realm failed");
    return;
  }
  JSValue init =
      JS_Eval(ui, "globalThis.frames = 0", sizeof("globalThis.frames = 0") - 1,
              "ui.js", JS_EVAL_TYPE_GLOBAL);
  JS_FreeValue(ui, init);
  for (uint32_t generation = 1; generation <= 2; ++generation) {
    const pocket_worker_esp_config config = {
        .struct_size = sizeof(config),
        .generation = generation,
        .nodes = nodes,
        .node_count = 2,
        .source = (const char *)worker_source_start,
        .source_size = (size_t)(worker_source_end - worker_source_start) - 1,
        .heap_limit = 384 * 1024,
        .stack_limit = 16 * 1024,
        .task_stack_size = 24 * 1024,
        .task_core = 1,
        .task_priority = 3};
    pocket_worker_esp *worker = NULL;
    const esp_err_t startup = pocket_worker_esp_start(&config, &worker);
    if (startup != ESP_OK) {
      ESP_LOGE("worker_test", "startup failed: %s", esp_err_to_name(startup));
      break;
    }
    pocket_worker_ui *bridge =
        pocket_worker_ui_attach(ui, pocket_worker_esp_core(worker));
    if (!bridge) {
      ESP_LOGE("worker_test", "channel attach failed");
      ESP_ERROR_CHECK(pocket_worker_esp_stop(worker, 5000));
      break;
    }
    const char channel_init[] =
        "globalThis.ch = __pocketWorkerChannels; ch.configure(1,1); "
        "ch.configure(2,0);"
        "globalThis.sent=0; globalThis.ack=0; globalThis.channelErrors=0; "
        "globalThis.frames=0;";
    JSValue channels = JS_Eval(ui, channel_init, sizeof(channel_init) - 1,
                               "channels.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(channels))
      ESP_LOGE("worker_test", "channel init failed");
    JS_FreeValue(ui, channels);
    const int64_t start = esp_timer_get_time();
    unsigned frames = 0;
    TickType_t previous_frame = xTaskGetTickCount();
    while (frames < 750) {
      ESP_ERROR_CHECK(pocket_worker_ui_latch(bridge) == POCKET_WORKER_OK
                          ? ESP_OK
                          : ESP_FAIL);
      const char ui_frame[] =
          "++frames; if(frames%50===1){let b=new Uint8Array(4);new "
          "DataView(b.buffer).setUint32(0,sent+1,true);"
          "if(ch.publish(2,b)===0)++sent;else ++channelErrors;}"
          "{let "
          "a=ch.observe(1),b=ch.observe(1);if(a.length!==b.length)++"
          "channelErrors;"
          "if(a.length){let x=new DataView(a[0].buffer).getUint32(0,true);"
          "if(x<ack||x>sent)++channelErrors;ack=x;"
          "if(a[0].some((v,i)=>v!==b[0][i]))++channelErrors;}}";
      JSValue frame = JS_Eval(ui, ui_frame, sizeof(ui_frame) - 1, "ui.js",
                              JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(frame))
        ESP_LOGE("worker_test", "UI frame failed");
      JS_FreeValue(ui, frame);
      ++frames;
      xTaskDelayUntil(&previous_frame, pdMS_TO_TICKS(20));
    }
    ESP_LOGI("worker_test",
             "generation=%lu ui_frames=%u elapsed_ms=%lld failed=%d",
             (unsigned long)generation, frames,
             (long long)((esp_timer_get_time() - start) / 1000),
             pocket_worker_esp_failed(worker));
    JSValue global = JS_GetGlobalObject(ui);
    JSValue sent_value = JS_GetPropertyStr(ui, global, "sent");
    JSValue ack_value = JS_GetPropertyStr(ui, global, "ack");
    JSValue error_value = JS_GetPropertyStr(ui, global, "channelErrors");
    int32_t sent = 0, ack = 0, errors = 0;
    JS_ToInt32(ui, &sent, sent_value);
    JS_ToInt32(ui, &ack, ack_value);
    JS_ToInt32(ui, &errors, error_value);
    ESP_LOGI("worker_test", "channels sent=%ld ack=%ld errors=%ld", (long)sent,
             (long)ack, (long)errors);
    JS_FreeValue(ui, sent_value);
    JS_FreeValue(ui, ack_value);
    JS_FreeValue(ui, error_value);
    JS_FreeValue(ui, global);
    pocket_worker_ui_detach(bridge);
    ESP_ERROR_CHECK(pocket_worker_esp_stop(worker, 5000));
  }
  JS_FreeContext(ui);
  JS_FreeRuntime(ui_runtime);
  const int32_t i2c_status = pocket_i2c_esp_destroy(i2c);
  const int32_t uart_status = pocket_uart_esp_destroy(uart);
  ESP_LOGI("worker_test", "shutdown i2c=%ld uart=%ld", (long)i2c_status,
           (long)uart_status);
}
