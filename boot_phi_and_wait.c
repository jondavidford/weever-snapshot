/*
 * This program checks for a doorbell from a modified
 * version of the Weever bootloader. 
 * It maps in the Phi's GDDR (low) 
 * memory into host memory, and looks for the value
 * 0xdeadbeef at a special address.
 *
 * Kyle C. Hale 2014
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>


#define PHI_PATH "/sys/class/mic/mic0/state"
//#define PHI_WEEVER  "boot:linux:/usr/share/mpss/boot/weever:/usr/share/mpss/boot/initramfs-knightscorner.cpio.gz"
#define PHI_WEEVER "boot:linux:/home/joncon/barrelfish/k1om/sbin/weever:/root/micstuff/naut_setup/nautilus.bin"

// in secs
#define DEFAULT_TIMEOUT 60
#define MAX_TIMEOUT    (60*5)

// This is where the host kernel has mapped the device's
// GDDR memory (8GB) using BARs
#define PHI_GDDR_ADDR     0x3fc00000000

// We only need a few megs of it though
#define PHI_GDDR_RANGE_SZ (1024*1024*512)

#define STATUS_ADDR 0x7a7a70
#define STATUS_OK 0xdeadbeef

#define AFTER_LOADER_STATUS_ADDR 0x7a7a74
#define AFTER_LOADER_STATUS_OK 0xdeadbead

#define NAUT_STATUS_ADDR 0x7a7a74
#define NAUT_OK 0xfeedfeed

// for mapping the Phi's physical memory
#define MEM "/dev/mem"


static void
usage (char * prog)
{
    fprintf(stderr, "Usage: %s [-t <timeout>]\n", prog);
    exit(0);
}


/* 
 * this will use Intel's Xeon Phi driver sysfs interface
 * to initiate a boot on the card using a custom image
 *
 */
static int
boot_phi (void)
{
    char * buf = PHI_WEEVER;
    int fd = 0;

    fd = open(PHI_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Couldn't open file %s (%s)\n", PHI_PATH,
                strerror(errno));
        return -1;
    }

    if (write(fd, buf, strlen(buf)) < 0) {
        fprintf(stderr, "Couldnt write file %s (%s)\n", PHI_PATH, strerror(errno));
        return -1;
    }

    close(fd);
    return 0;
}


/* 
 * test our well-known address for the magic
 * cookie. Return 1 if we find it, 0 otherwise
 *
 */
static int 
pollit (void * base, 
        uint64_t offset,
        uint32_t doorbell_val)
{
    uint32_t val = 0;
    int i;

    val = *(uint32_t*)((char*)base + offset);

    if (val == doorbell_val) {
        return 1;
    }

    return 0;
}

static int
wait_for_doorbell (void * base,
                   uint64_t offset,
                   uint32_t doorbell_val,
                   uint32_t timeout,
                   const char * ok_msg)
{
    uint8_t ping_recvd = 0;

    /* need to give the card some time to boot */
    while (timeout) {

        if (pollit(base, offset, doorbell_val)) {
            ping_recvd = 1;
            break;
        }

        putchar('.');
        fflush(stdout);

        sleep(1);
        --timeout;
    }

    if (ping_recvd) {
        printf("OK: %s\n", ok_msg);
    } else {
        printf("FAIL: timeout\n");
        return -1;
    }

    return 0;
}
            

int 
main (int argc, char ** argv)
{
    unsigned timeout   = DEFAULT_TIMEOUT;
    int memfd          = 0;
    uint64_t range_sz = PHI_GDDR_RANGE_SZ;
    void * gddr_addr   = (void*)PHI_GDDR_ADDR;

    if (argc > 1 && strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
    }

    if (argc > 1 && strcmp(argv[1], "-t") == 0) {

        if (argc > 2) {
            timeout = atoi(argv[2]);
        }

        if (timeout > MAX_TIMEOUT) {
            timeout = MAX_TIMEOUT;
        } 

    } 

    /* what we're doing here is mapping in /dev/mem into this process. 
     * if you're not familiar, /dev/mem is a device file that allows you
     * to read/write the physical memory of the machine. It just so happens 
     * that some of the *host's* physical memory corresponds to the Phi card's
     * memory. We're going to leverage that
     */
    if (!(memfd = open(MEM, O_RDONLY))) {
        fprintf(stderr, "Error opening file %s\n", MEM);
        return -1;
    } 

    printf("Mapping MIC 0 MMIO memory range (%u bytes at %p)\n",
            range_sz,
            gddr_addr);

    void * buf = mmap(NULL, 
                      range_sz,
                      PROT_READ,
                      MAP_PRIVATE, 
                      memfd,
                      (unsigned long long)gddr_addr);

    if (buf == MAP_FAILED) {
        fprintf(stderr, "Couldn't map PHI MMIO range\n");
        return -1;
    }

    printf("Booting PHI with custom image...");

    if (!boot_phi()) {
        printf("OK\n");
    } else {
        printf("FAIL\n");
        exit(1);
    }

#if 0
    printf("Waiting for doorbell from Weever in GDDR range (timeout=%us)\n", timeout);

    if (wait_for_doorbell(buf,
                      STATUS_ADDR,
                      STATUS_OK,
                      timeout,
                      "Weever up and running") == -1) {
        fprintf(stderr, "Exiting\n");
        exit(1);
    }

    printf("Waiting for doorbell from Weever after it calls the 'loader' function (timeout=%us)\n", timeout);
    if (wait_for_doorbell(buf,
                      STATUS_ADDR+4,
                      STATUS_OK,
                      timeout,
                      "Weever about to call loader") == -1) {
        fprintf(stderr, "Exiting\n");
        exit(1);
    }
#endif 

    printf("Dump of status prints:\n");

    int i = 0;
    int j = 0;
    while (1) {
        unsigned char *o = (unsigned char*) (buf+STATUS_ADDR);
       
        printf("------------------------------------------\n"); 
        for (i = 0; i < 512; i+=16) {
            printf("%04x:\t", i);
            for (j=i;j<i+16;j++) { 
               printf("%02x",o[j]);
            }
            printf("\t");
            for (j=i;j<i+16;j++) { 
               if (isprint(o[j])) { 
                 putchar(o[j]);
               } else {
                 putchar('.');
               }
            }
            printf("\n");
        }
        fflush(stdout);
        sleep(2);
    }

    printf("Waiting for doorbell after loader (timeout=%us)\n", timeout);

    if (wait_for_doorbell(buf,
                        STATUS_ADDR+8,
                        STATUS_OK,
                        timeout,
                        "Weever after loader running") == -1) {
        fprintf(stderr, "Exiting\n");
        exit(1);
    }

    return 0;
}
