/* Here is a helper function that warns you if someone tries to sniff your
 * network traffic (i.e. a Man-In-The-Middle attack ran against you thanks
 * to ARP cache poisoning).
 *
 * It must be called regularly because it monitors changes in the ARP table
 * If a host got a new MAC address, it will alert during ALERT_TIMEOUT seconds.
 * If the MAC address remains the same, it assumes it is just a new host.
 * Otherwise, if it keep changing, it will keep on alerting.
 *
 * Returns true on success, false otherwise.
 *
 * Written by vladz (vladz AT devzero.fr).
 * Updated by mephesto1337 ( dwm-status AT junk-mail.fr )
 */

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


// Some useful macros
#define CHK(expr, cond)         \
    do { \
        if ( (expr) cond ) { \
            fprintf(stderr, "%s failed", #expr); \
            goto fail; \
        } \
    } while ( 0 )

#define CHK_NEG(expr)           CHK((long)(expr), < 0L)
#define CHK_FALSE(expr)         CHK(!!(expr), == false)
#define CHK_NULL(expr)          CHK(expr, == NULL)
#define SAFE_FREE(func, ptr)    \
    do { \
        if ( ptr != NULL ) { \
            func(ptr); \
        } \
        ptr = NULL; \
    } while ( 0 )
#define ARRAY_SIZE(x)           (sizeof(x) / sizeof(x[0]))

#define ALERT_TIMEOUT ((time_t)40L) // In seconds

/* The hard maximum number of entries kept in the ARP cache is obtained via
 * "sysctl net.ipv4.neigh.default.gc_thresh3" (see arp(7)).  Default value
 * for Linux is 1024.
 */
#define MAX_ARP_CACHE_ENTRIES  1024


struct ether_ip_s {
    union {
        uint8_t mac[ETH_ALEN];
        unsigned long lmac;
    };
    time_t last_changed;
    in_addr_t ip;
};

struct ether_ip_s table[MAX_ARP_CACHE_ENTRIES];
size_t table_size = 0;


bool lookup_and_insert(const struct ether_ip_s *new);

bool check_arp_table(char *message, size_t len) {
    FILE *f;
    struct ether_ip_s tmp;
    char ip_address[32];
    char mac_address[32];

    snprintf(message, len, "ARP table OK");
    CHK_NULL(f = fopen("/proc/net/arp", "r"));
    time(&tmp.last_changed);
    fscanf(f, "%*[^\n]\n");
    while ( !feof(f) ) {
        CHK(fscanf(f, "%s%*[ ]0x%*x%*[ ]0x%*x%*[ ]%[a-f0-9:]%*[^\n]\n", ip_address, mac_address), != 2);
        CHK_NEG(inet_pton(AF_INET, ip_address, &tmp.ip));

        tmp.lmac = 0UL;
        CHK(sscanf(
            mac_address, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            &tmp.mac[0], &tmp.mac[1], &tmp.mac[2], &tmp.mac[3], &tmp.mac[4], &tmp.mac[5]
        ), != 6);

        if ( ! lookup_and_insert(&tmp) ) {
            snprintf(message, len, "Possible MITM attack, please check %s", ip_address);
            break;
        }
    }
    SAFE_FREE(fclose, f);
    return true;

    fail:
    SAFE_FREE(fclose, f);
    snprintf(message, len, "ARP table ???");
    return false;
}

bool lookup_and_insert(const struct ether_ip_s *new) {
    for ( size_t i = 0; i < table_size; i++ ) {
        if ( table[i].ip == new->ip ) {
            if ( table[i].lmac != new->lmac ) {
                if ( table[i].last_changed + ALERT_TIMEOUT > new->last_changed ) {
                    return false;
                } else {
                    // Update the DB, it must be a new host
                    table[i].lmac = new->lmac;
                    table[i].last_changed = new->last_changed;
                    return true;
                }
            } else {
                // Update last seen
                table[i].last_changed = new->last_changed;
                return true;
            }
        }
    }

    if ( table_size < ARRAY_SIZE(table) ) {
        memcpy(&table[table_size], new, sizeof(struct ether_ip_s));
        table_size++;
    } else {
        // To big, let's restart from the begining
        table_size = 0;
        return false;
    }

    return true;
}
