/**
* @file log.c
* @author rigensen
* @brief 
* @date 二  5/19 12:01:09 2020
*/
#include "public.h"
#include "mqtt.h"
#include "sockets.h"

static struct mqtt_client client;
static uint8_t sendbuf[2048];
static uint8_t recvbuf[1024];

int get_mac_addr(char *addr)
{
    int sock, i;
    struct ifreq ifreq;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("create socket error\n");
        goto err;
    }
    strcpy(ifreq.ifr_name, "eth0");
    if(ioctl(sock, SIOCGIFHWADDR, &ifreq)<0) {
        perror("SIOCGIFHWADDR");
        goto err;
    }
    for (i=0; i<6; i++) {
        sprintf(addr+i*2, "%02x", (unsigned char)ifreq.ifr_hwaddr.sa_data[i]);
    }
    return 0;
err:
    return -1;
}

static void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    printf("publish callback in\n");
}

static void* client_refresher(void* client)
{
    printf("client refresher enter...\n");
    for(;;) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

int log_init(char *mqtt_host, char * port,  char *user, char *passwd)
{
    int sockfd;
    char mac[16] = {0};
    pthread_t client_daemon;

    if (get_mac_addr(mac) < 0)
        goto err;
    sockfd = open_nb_socket(mqtt_host, port);
    if (sockfd == -1) {
        perror("Failed to open socket: ");
        goto err;
    }
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);
    mqtt_connect(&client, mac, NULL, NULL, 0, user, passwd, MQTT_CONNECT_CLEAN_SESSION, 400);
    if (client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        goto err;
    } else {
        printf("mqtt connect %s:%s success\n", mqtt_host, port);
    }
    if(pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        goto err;
    }

    return 0;
err:
    return -1;
}

void log_out(char *file, const char *function, int line, char *fmt, ...)
{
    char buf[1024] = {0};
    va_list arg;
    time_t t = time(NULL);
    struct tm *tm_now = localtime(&t);
    static char topic[16] = {0};
    enum MQTTErrors err;

    if (!strlen(topic)) {
        if( get_mac_addr(topic) < 0) {
            printf("get mac address error\n");
            return;
        }
        printf("mac address: %s\n", topic);
    }
    strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S]", tm_now);
    sprintf(buf+strlen(buf), " %s:%d(%s)$ ", file, line, function);
    va_start(arg, fmt);
    vsprintf(buf+strlen(buf), fmt, arg);
    va_end(arg);
    err = mqtt_publish(&client, topic, buf, strlen(buf), MQTT_PUBLISH_QOS_1);
}

