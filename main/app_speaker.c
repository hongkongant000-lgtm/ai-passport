#include "app_speaker.h"
#include "app_audio_mode.h"
#include "app_config.h"
#include "app_wifi.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "app_speaker";
static const char *HEADER = "PASSPORT";

static TaskHandle_t s_task;
static volatile bool s_run;
static volatile bool s_playing;
static volatile bool s_stop;
static uint8_t s_volume = 60;
static int s_listen_fd = -1;

uint16_t app_speaker_port(void) { return APP_SPEAKER_TCP_PORT; }
uint8_t app_speaker_volume(void) { return s_volume; }
bool app_speaker_running(void) { return s_run; }
bool app_speaker_playing(void) { return s_playing; }

void app_speaker_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_volume = percent;
    bsp_audio_set_volume(s_volume);
}

static int recv_all(int fd, void *buf, size_t len, int timeout_ms)
{
    uint8_t *p = buf;
    size_t got = 0;
    while (got < len && !s_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sel == 0) return (int)got; // timeout — return partial
        int n = recv(fd, p + got, len - got, 0);
        if (n == 0) return (int)got;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)n;
    }
    return (int)got;
}

static void serve_client(int fd)
{
    char hdr[8];
    int n = recv_all(fd, hdr, sizeof(hdr), 3000);
    if (n != 8 || memcmp(hdr, HEADER, 8) != 0) {
        ESP_LOGW(TAG, "bad header from client");
        return;
    }
    if (bsp_audio_set_format(APP_AUDIO_HZ, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "audio format failed");
        return;
    }
    bsp_audio_set_volume(s_volume);
    s_playing = true;
    ESP_LOGI(TAG, "streaming playback");

    int16_t buf[512];
    while (!s_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
        int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) break;
        if (sel == 0) continue; // idle — keep socket open
        n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) break; // peer closed
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        n -= n % (int)sizeof(int16_t);
        if (n <= 0) continue;
        if (bsp_audio_write(buf, (size_t)n) != ESP_OK) {
            ESP_LOGW(TAG, "pcm write failed");
            break;
        }
    }
    s_playing = false;
}

static void speaker_task(void *arg)
{
    (void)arg;
    if (!app_audio_mode_try_speaker()) {
        ESP_LOGW(TAG, "audio busy; speaker not started");
        s_run = false;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s_listen_fd < 0) {
        ESP_LOGE(TAG, "socket failed");
        goto done;
    }
    int yes = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(APP_SPEAKER_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(s_listen_fd, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen failed: errno=%d", errno);
        goto done;
    }
    ESP_LOGI(TAG, "listening on TCP %d for PASSPORT PCM stream", APP_SPEAKER_TCP_PORT);

    while (!s_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s_listen_fd, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 300000 };
        int sel = select(s_listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int fd = accept(s_listen_fd, (struct sockaddr *)&client, &clen);
        if (fd < 0) continue;
        ESP_LOGI(TAG, "client connected");
        serve_client(fd);
        close(fd);
        ESP_LOGI(TAG, "client disconnected");
    }

done:
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    s_playing = false;
    app_audio_mode_end_speaker();
    s_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t app_speaker_start(void)
{
    if (s_run) return ESP_OK;
    if (!app_wifi_got_ip()) return ESP_ERR_INVALID_STATE;
    s_stop = false;
    s_run = true;
    if (xTaskCreate(speaker_task, "app_speaker", 6144, NULL, 5, &s_task) != pdPASS) {
        s_run = false;
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_speaker_stop(void)
{
    if (!s_run) return;
    s_stop = true;
    if (s_listen_fd >= 0) {
        shutdown(s_listen_fd, SHUT_RDWR);
    }
    // Task clears s_run when exiting.
    for (int i = 0; i < 50 && s_run; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
