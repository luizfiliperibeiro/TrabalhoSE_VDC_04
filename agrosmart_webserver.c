#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"
#include "lwip/netif.h"

#define WIFI_SSID "SEU_SSID"
#define WIFI_PASSWORD "SUA_SENHA"

// GPIOs
#define BUZZER_PIN 15
#define LED_ALERTA_PIN 13  // LED vermelho
#define ADC_CHANNEL 0      // ADC0 para o potenciômetro do joystick

bool alerta = false;

void inicializa_hardware(void) {
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    gpio_put(BUZZER_PIN, 0);

    gpio_init(LED_ALERTA_PIN);
    gpio_set_dir(LED_ALERTA_PIN, GPIO_OUT);
    gpio_put(LED_ALERTA_PIN, 0);

    adc_init();
    adc_gpio_init(26); // GPIO26 = ADC0
    adc_select_input(ADC_CHANNEL);
}

float ler_umidade(void) {
    uint16_t raw = adc_read();
    float umidade_percent = 100.0f - ((raw * 100.0f) / 4095.0f);
    return umidade_percent;
}

void verificar_alerta(float umidade) {
    if (umidade < 30.0f) {
        alerta = true;
        gpio_put(LED_ALERTA_PIN, 1);
        gpio_put(BUZZER_PIN, 1);
    }
}

void resetar_alerta(void) {
    alerta = false;
    gpio_put(LED_ALERTA_PIN, 0);
    gpio_put(BUZZER_PIN, 0);
}

// Requisições HTML
void tratar_requisicao(char *req) {
    if (strstr(req, "GET /resetar") != NULL) {
        resetar_alerta();
    }
}

// Função de callback ao aceitar conexão
static err_t aceitar_conexao(void *arg, struct tcp_pcb *newpcb, err_t err) {
    extern err_t receber_requisicao(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
    tcp_recv(newpcb, receber_requisicao);
    return ERR_OK;
}

// Callback de recebimento HTTP
err_t receber_requisicao(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = malloc(p->len + 1);
    memcpy(req, p->payload, p->len);
    req[p->len] = '\0';
    printf("Requisição: %s\n", req);

    tratar_requisicao(req);
    float umidade = ler_umidade();
    if (!alerta) verificar_alerta(umidade);

    char html[1024];
    snprintf(html, sizeof(html),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE html><html><head><title>AgroSmart</title><style>"
        "body { font-family: sans-serif; text-align: center; background-color: #e6ffe6; }"
        "h1 { font-size: 40px; margin-top: 30px; }"
        ".info { font-size: 28px; margin-top: 20px; }"
        "button { font-size: 24px; padding: 10px 30px; margin-top: 30px; }"
        "</style></head><body>"
        "<h1>AgroSmart - Umidade do Solo</h1>"
        "<p class='info'>Umidade atual: %.2f%%</p>"
        "%s"
        "<form action='/resetar'><button>Resetar Alerta</button></form>"
        "</body></html>",
        umidade,
        alerta ? "<p class='info' style='color:red;'>Alerta: Umidade muito baixa!</p>" : "<p class='info' style='color:green;'>Nível dentro do ideal.</p>"
    );

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    free(req);
    pbuf_free(p);
    return ERR_OK;
}

// MAIN
int main() {
    stdio_init_all();
    inicializa_hardware();

    if (cyw43_arch_init()) {
        printf("Erro ao iniciar o módulo CYW43\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Falha ao conectar\n");
        sleep_ms(2000);
    }
    printf("Conectado!\n");

    if (netif_default)
        printf("IP: %s\n", ipaddr_ntoa(&netif_default->ip_addr));

    struct tcp_pcb *server = tcp_new();
    if (!server || tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Erro ao iniciar servidor\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, aceitar_conexao);
    printf("Servidor pronto na porta 80\n");

    while (true) {
        cyw43_arch_poll();
        sleep_ms(100);
    }

    cyw43_arch_deinit();
    return 0;
}