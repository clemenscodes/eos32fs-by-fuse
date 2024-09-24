/*
 * mkgpt.c -- make GUID partition table on a disk image
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <uuid/uuid.h>


#define SECTOR_SIZE		512
#define MIN_NUMBER_SECTORS	4096

#define NUMBER_PART_ENTRIES	128
#define SIZEOF_PART_ENTRY	128
#define NUMBER_PART_BYTES	(NUMBER_PART_ENTRIES * SIZEOF_PART_ENTRY)
#define NUMBER_PART_SECTORS	(NUMBER_PART_BYTES) / SECTOR_SIZE
#define FIRST_MNGR_SECTOR	(2 + NUMBER_PART_SECTORS)
#define NUMBER_MNGR_SECTORS	2014
#define FIRST_USABLE_SECTOR	(FIRST_MNGR_SECTOR + NUMBER_MNGR_SECTORS)


/**************************************************************/


void error(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Error: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  exit(1);
}


/**************************************************************/


#define CRC32_POLY	0x04C11DB7	/* normal form */
#define CRC32_POLY_REV	0xEDB88320	/* reverse form */
#define CRC32_INIT_XOR	0xFFFFFFFF
#define CRC32_FINAL_XOR	0xFFFFFFFF


static unsigned int crc32Table[256];


void crc32Init(void) {
  unsigned int c;
  int n, k;

  for (n = 0; n < 256; n++) {
    c = n;
    for (k = 0; k < 8; k++) {
      if ((c & 1) != 0) {
        c = (c >> 1) ^ CRC32_POLY_REV;
      } else {
        c = c >> 1;
      }
    }
    crc32Table[n] = c;
  }
}


unsigned int crc32Sum(unsigned char *buffer, unsigned int size) {
  int i;
  unsigned int crc32;

  crc32 = CRC32_INIT_XOR;
  for (i = 0; i < size; i++) {
    crc32 = (crc32 >> 8) ^ crc32Table[((crc32 ^ buffer[i]) & 0xFF)];
  }
  return crc32 ^ CRC32_FINAL_XOR;
}


/**************************************************************/


unsigned int get4LE(unsigned char *addr) {
  return (((unsigned int) *(addr + 0)) <<  0) |
         (((unsigned int) *(addr + 1)) <<  8) |
         (((unsigned int) *(addr + 2)) << 16) |
         (((unsigned int) *(addr + 3)) << 24);
}


void put4LE(unsigned char *addr, unsigned int val) {
  *(addr + 0) = (val >>  0) & 0xFF;
  *(addr + 1) = (val >>  8) & 0xFF;
  *(addr + 2) = (val >> 16) & 0xFF;
  *(addr + 3) = (val >> 24) & 0xFF;
}


void put8LE(unsigned char *addr, unsigned int val) {
  put4LE(addr + 0, val);
  put4LE(addr + 4, 0);
}


void xchg4LE(unsigned char *addr1, unsigned char *addr2) {
  unsigned int val1, val2;

  val1 = get4LE(addr1);
  val2 = get4LE(addr2);
  put4LE(addr1, val2);
  put4LE(addr2, val1);
}


void xchg8LE(unsigned char *addr1, unsigned char *addr2) {
  xchg4LE(addr1 + 0, addr2 + 0);
  xchg4LE(addr1 + 4, addr2 + 4);
}


/**************************************************************/


void uuid_copyLE(unsigned char *dst, unsigned char *src) {
  int i;

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
  dst[4] = src[5];
  dst[5] = src[4];
  dst[6] = src[7];
  dst[7] = src[6];
  for (i = 8; i < 16; i++) {
    dst[i] = src[i];
  }
}


/**************************************************************/


void wrSector(FILE *disk, unsigned int sectorNum, unsigned char *buf) {
  if (fseek(disk, (unsigned long) sectorNum * SECTOR_SIZE, SEEK_SET) < 0) {
    error("cannot position to sector %u (0x%X)", sectorNum, sectorNum);
  }
  if (fwrite(buf, 1, SECTOR_SIZE, disk) != SECTOR_SIZE) {
    error("cannot write sector %u (0x%X)", sectorNum, sectorNum);
  }
}


/**************************************************************/


void makeProtectiveMBR(unsigned char *buf, unsigned int numSectors) {
  int i;

  /* boot code: offset 0, length 440 */
  /* not filled in here, set to zero */
  for (i = 0; i < 440; i++) {
    buf[i] = 0x00;
  }
  /* MBR disk signature: offset 440, length 4 */
  /* unused, set to zero */
  for (i = 440; i < 444; i++) {
    buf[i] = 0x00;
  }
  /* unknown: offset 444, length 2 */
  /* unused, set to zero */
  buf[444] = 0x00;
  buf[445] = 0x00;
  /* 4 partition records: offset 446, length 4*16 */
  /* the first record holds the protective MBR partition record */
  /* the next 3 records are empty */
  buf[446] = 0x00;  /* boot indicator: not bootable */
  buf[447] = 0x00;  /* starting CHS: 0x000200, see starting LBA */
  buf[448] = 0x02;  /* ditto */
  buf[449] = 0x00;  /* ditto */
  buf[450] = 0xEE;  /* OS type: GPT protective */
  buf[451] = 0xFF;  /* ending CHS: 0xFFFFFF (cannot be represented) */
  buf[452] = 0xFF;  /* ditto */
  buf[453] = 0xFF;  /* ditto */
  put4LE(&buf[454], 0x00000001);  /* starting LBA: GPT partition header */
  put4LE(&buf[458], numSectors - 1);  /* size in LBA: size of disk - 1 */
  for (i = 462; i < SECTOR_SIZE - 2; i++) {
    buf[i] = 0x00;
  }
  /* signature: offset 510, length 2 */
  buf[SECTOR_SIZE - 2] = 0x55;
  buf[SECTOR_SIZE - 1] = 0xAA;
}


void readBootCode(unsigned char *protMBR, char *bootName) {
  FILE *boot;
  unsigned long bootSize;

  /* open boot code file */
  boot = fopen(bootName, "rb");
  if (boot == NULL) {
    error("cannot open boot code file '%s'", bootName);
  }
  /* determine boot code size */
  fseek(boot, 0, SEEK_END);
  bootSize = ftell(boot);
  fseek(boot, 0, SEEK_SET);
  if (bootSize > 440) {
    error("boot code '%s' is too big to fit (max 440 bytes, has %lu)",
          bootName, bootSize);
  }
  /* read boot code into protective MBR */
  if (fread(protMBR, 1, bootSize, boot) != bootSize) {
    error("cannot read boot code file '%s'", bootName);
  }
  printf("Boot code (%lu bytes) read from file '%s'.\n",
         bootSize, bootName);
  fclose(boot);
}


void readMngrCode(unsigned char *mngrCode, char *mngrName) {
  FILE *mngr;
  unsigned long mngrSize;
  int mngrSectors;

  /* open manager code file */
  mngr = fopen(mngrName, "rb");
  if (mngr == NULL) {
    error("cannot open manager code file '%s'", mngrName);
  }
  /* determine manager code size */
  fseek(mngr, 0, SEEK_END);
  mngrSize = ftell(mngr);
  fseek(mngr, 0, SEEK_SET);
  mngrSectors = (mngrSize + SECTOR_SIZE - 1) / SECTOR_SIZE;
  if (mngrSectors > NUMBER_MNGR_SECTORS) {
    error("manager code '%s' is too big to fit (max %d sectors, has %d)",
          mngrName, NUMBER_MNGR_SECTORS, mngrSectors);
  }
  /* read manager code */
  if (fread(mngrCode, 1, mngrSize, mngr) != mngrSize) {
    error("cannot read manager code file '%s'", mngrName);
  }
  printf("Manager code (%lu bytes) read from file '%s'.\n",
         mngrSize, mngrName);
  fclose(mngr);
}


/**************************************************************/


unsigned int makePartTable(unsigned char *buf) {
  int i;
  unsigned int partTableCRC;

  for (i = 0; i < NUMBER_PART_ENTRIES; i++) {
    memset(&buf[i * SIZEOF_PART_ENTRY], 0, SIZEOF_PART_ENTRY);
  }
  partTableCRC = crc32Sum(buf, NUMBER_PART_BYTES);
  return partTableCRC;
}


void makePartTblHdr(unsigned char *buf,
                    unsigned int numSectors,
                    unsigned int partTableCRC) {
  uuid_t diskUUID;
  char diskUUIDstr[40];
  int i;
  unsigned int headerCRC;

  /* signature */
  strcpy((char *) &buf[0], "EFI PART");
  /* revision */
  put4LE(&buf[8], 0x00010000);
  /* header size */
  put4LE(&buf[12], 92);
  /* header CRC32 */
  /* set to zero here, will be overwritten later */
  put4LE(&buf[16], 0x00000000);
  /* reserved, set to zero */
  put4LE(&buf[20], 0x00000000);
  /* my LBA */
  put8LE(&buf[24], 0x00000001);
  /* alternate LBA */
  put8LE(&buf[32], numSectors - 1);
  /* first usable LBA */
  put8LE(&buf[40], FIRST_USABLE_SECTOR);
  /* last usable LBA */
  put8LE(&buf[48], numSectors - 2 - NUMBER_PART_SECTORS);
  /* disk UUID */
  uuid_generate(diskUUID);
  uuid_unparse_upper(diskUUID, diskUUIDstr);
  printf("Disk identifier (UUID): %s\n", diskUUIDstr);
  uuid_copyLE(&buf[56], diskUUID);
  /* partition entry LBA */
  put8LE(&buf[72], 2);
  /* number of partition entries */
  put4LE(&buf[80], NUMBER_PART_ENTRIES);
  /* size of partition entry */
  put4LE(&buf[84], SIZEOF_PART_ENTRY);
  /* partition entry array CRC32 */
  put4LE(&buf[88], partTableCRC);
  /* reserved, set to zero */
  for (i = 92; i < SECTOR_SIZE; i++) {
    buf[i] = 0x00;
  }
  /* now correct header CRC32 */
  headerCRC = crc32Sum(buf, 92);
  put4LE(&buf[16], headerCRC);
}


void makeBackupTblHdr(unsigned char *buf,
                      unsigned char *partTblHdr,
                      unsigned int numSectors) {
  unsigned int backupCRC;

  memcpy(buf, partTblHdr, SECTOR_SIZE);
  xchg8LE(&buf[24], &buf[32]);
  put8LE(&buf[72], numSectors - 1 - NUMBER_PART_SECTORS);
  put4LE(&buf[16], 0x00000000);
  backupCRC = crc32Sum(buf, 92);
  put4LE(&buf[16], backupCRC);
}


/**************************************************************/


int main(int argc, char *argv[]) {
  char *diskName;
  char *bootName;
  char *mngrName;
  FILE *disk;
  unsigned long diskSize;
  unsigned int numSectors;
  unsigned char protMBR[SECTOR_SIZE];
  unsigned char mngrCode[NUMBER_MNGR_SECTORS * SECTOR_SIZE];
  unsigned char partTblHdr[SECTOR_SIZE];
  unsigned char backupTblHdr[SECTOR_SIZE];
  unsigned char partTable[NUMBER_PART_BYTES];
  unsigned int partTableCRC;
  int s;

  /* check command line arguments */
  if (argc < 2 || argc > 4) {
    printf("Usage: %s <disk image> [<boot code> [<boot manager>]]\n",
           argv[0]);
    exit(1);
  }
  diskName = argv[1];
  bootName = NULL;
  mngrName = NULL;
  if (argc > 2) {
    bootName = argv[2];
  }
  if (argc > 3) {
    mngrName = argv[3];
  }
  /* initialize CRC32 table */
  crc32Init();
  /* open disk image */
  disk = fopen(diskName, "r+b");
  if (disk == NULL) {
    error("cannot open disk image '%s'", diskName);
  }
  /* determine disk size */
  fseek(disk, 0, SEEK_END);
  diskSize = ftell(disk);
  numSectors = diskSize / SECTOR_SIZE;
  printf("Disk '%s' has %u (0x%X) sectors.\n",
         diskName, numSectors, numSectors);
  if (numSectors < MIN_NUMBER_SECTORS) {
    error("disk is too small to be useful (minimum size is %d sectors)",
          MIN_NUMBER_SECTORS);
  }
  if (diskSize % SECTOR_SIZE != 0) {
    printf("Warning: disk size is not a multiple of sector size!\n");
  }
  /* write protective MBR */
  makeProtectiveMBR(protMBR, numSectors);
  if (bootName != NULL) {
    readBootCode(protMBR, bootName);
  }
  wrSector(disk, 0, protMBR);
  /* write primary partition table */
  partTableCRC = makePartTable(partTable);
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    wrSector(disk, 2 + s, &partTable[s * SECTOR_SIZE]);
  }
  /* possibly write boot manager */
  if (mngrName != NULL) {
    readMngrCode(mngrCode, mngrName);
    for (s = 0; s < NUMBER_MNGR_SECTORS; s++) {
      wrSector(disk, FIRST_MNGR_SECTOR + s, &mngrCode[s * SECTOR_SIZE]);
    }
  }
  /* write primary partition table header */
  makePartTblHdr(partTblHdr, numSectors, partTableCRC);
  wrSector(disk, 1, partTblHdr);
  /* write backup partition table */
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    wrSector(disk, numSectors - 1 - NUMBER_PART_SECTORS + s,
             &partTable[s * SECTOR_SIZE]);
  }
  /* write backup partition table header */
  makeBackupTblHdr(backupTblHdr, partTblHdr, numSectors);
  wrSector(disk, numSectors - 1, backupTblHdr);
  /* close disk image and exit */
  fclose(disk);
  return 0;
}
