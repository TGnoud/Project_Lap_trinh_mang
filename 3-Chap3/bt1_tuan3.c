#define _POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Host byte order:
 *   Thu tu byte ma CPU cua may dang su dung.
 *   Vi du may x86/x86-64 thuong dung little-endian.
 *
 * Network byte order:
 *   Thu tu byte chuan dung khi truyen du lieu tren mang,
 *   luon la big-endian.
 */

static void print_ipv4(const struct addrinfo *p)
{
    char ip[INET_ADDRSTRLEN];
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)p->ai_addr;
    const unsigned char *bytes = (const unsigned char *)&addr4->sin_addr;
    uint32_t network_value = addr4->sin_addr.s_addr;
    /* Chuyen network byte order -> host byte order */
    uint32_t host_value = ntohl(network_value);
    /* Chuyen host byte order -> network byte order */
    uint32_t restored_network_value = htonl(host_value);

    if (inet_ntop(AF_INET, &addr4->sin_addr, ip, sizeof(ip)) == NULL)
    {
        perror("inet_ntop IPv4");
        return;
    }

    printf("IPv4\n");
    printf("  IP            : %s\n", ip);

    /*
     * sin_port duoc luu theo network byte order.
     * Khong nen in truc tiep vi tren may little-endian
     * gia tri co the bi dao byte.
     * ntohs() chuyen network byte order -> host byte order.
     */
    printf("  Port          : %u\n", (unsigned int)ntohs(addr4->sin_port));
    printf("  IP bytes      : %u %u %u %u\n",
           (unsigned int)bytes[0], (unsigned int)bytes[1],
           (unsigned int)bytes[2], (unsigned int)bytes[3]);
    printf("  Network value : 0x%08x\n", (unsigned int)network_value);
    printf("  Host value    : 0x%08x\n", (unsigned int)host_value);
    printf("  htonl(Host)   : 0x%08x\n", (unsigned int)restored_network_value);
}

static void print_ipv6(const struct addrinfo *p)
{
    char ip[INET6_ADDRSTRLEN];
    const struct sockaddr_in6 *addr6 =
        (const struct sockaddr_in6 *)p->ai_addr;

    if (inet_ntop(AF_INET6, &addr6->sin6_addr, ip, sizeof(ip)) == NULL)
    {
        perror("inet_ntop IPv6");
        return;
    }

    printf("IPv6\n");
    printf("  IP            : %s\n", ip);
    printf("  Port          : %u\n", (unsigned int)ntohs(addr6->sin6_port));
}

static int print_test_sockaddr(void)
{
    struct sockaddr_in test_addr;
    char ip[INET_ADDRSTRLEN];

    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = AF_INET;
    /*
     * Port la gia tri 16 bit.
     * htons(): host byte order -> network byte order.
     */
    test_addr.sin_port = htons(5555);
    /*
     * inet_pton():
     * "192.168.1.10"
     *        ↓
     * dia chi IPv4 dang nhi phan trong sin_addr
     */
    if (inet_pton(AF_INET, "192.168.1.10", &test_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Khong the tao dia chi IPv4 test.\n");
        return 1;
    }
    /*
     * Chuyen dia chi nhi phan nguoc lai thanh chuoi.
     */
    if (inet_ntop(AF_INET, &test_addr.sin_addr, ip, sizeof(ip)) == NULL)
    {
        perror("inet_ntop test");
        return 1;
    }

    printf("Test sockaddr_in:\n");
    printf("  IP            : %s\n", ip);
    printf("  Port          : %u\n", (unsigned int)ntohs(test_addr.sin_port));
    return 0;
}

int main(int argc, char *argv[])
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    int ret;

    if (argc != 2)
    {
        fprintf(stderr, "Su dung: %s <ten_mien>\n", argv[0]);
        return 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* Lay ca IPv4 va IPv6. */
    hints.ai_socktype = SOCK_STREAM;  /* Dich vu HTTP/TCP, cong 80. */

    ret = getaddrinfo(argv[1], "80", &hints, &res);
    if (ret != 0)
    {
        fprintf(stderr, "getaddrinfo(%s): %s\n", argv[1], gai_strerror(ret));
        return 1;
    }

    printf("Domain: %s\n", argv[1]);
    for (p = res; p != NULL; p = p->ai_next)
    {
        /* ai_addr co kieu struct sockaddr * tong quat; can ep kieu dung
           theo ai_family truoc khi doc cac truong chuyen biet. */
        if (p->ai_family == AF_INET)
            print_ipv4(p);
        else if (p->ai_family == AF_INET6)
            print_ipv6(p);
    }

    freeaddrinfo(res);
    return print_test_sockaddr();
}
