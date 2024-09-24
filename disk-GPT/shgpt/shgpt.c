/*
 * shgpt.c -- show GUID partition table on a disk image
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


typedef enum { false, true } bool;


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


void warning(char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("Warning: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
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


bool isZero(unsigned char *buf, int len) {
  unsigned char res;
  int i;

  res = 0;
  for (i = 0; i < len; i++) {
    res |= buf[i];
  }
  return res == 0;
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


void rdSector(FILE *disk, unsigned int sectorNum, unsigned char *buf) {
  if (fseek(disk, (unsigned long) sectorNum * SECTOR_SIZE, SEEK_SET) < 0) {
    error("cannot position to sector %u (0x%X)", sectorNum, sectorNum);
  }
  if (fread(buf, 1, SECTOR_SIZE, disk) != SECTOR_SIZE) {
    error("cannot read sector %u (0x%X)", sectorNum, sectorNum);
  }
}


/**************************************************************/


void showProtectiveMBR(unsigned char *buf) {
  int i;

  /* show */
  printf("\nProtective MBR\n");
  for (i = 0; i < 4; i++) {
    printf("    MBR partition %d  : ", i + 1);
    if (isZero(&buf[446 + i * 16], 16)) {
      printf("-- empty --");
    } else {
      if (buf[450 + i * 16] == 0xEE) {
        printf("GPT protective");
      } else
      if (buf[450 + i * 16] == 0xEF) {
        printf("UEFI system");
      } else {
        printf("-- unknown --");
      }
    }
    printf("\n");
  }
  printf("    signature [510]  : 0x%02X\n", buf[510]);
  printf("    signature [511]  : 0x%02X\n", buf[511]);
  /* check */
  if (buf[450] != 0xEE) {
    error("protective MBR has wrong OS type in partition 1");
  }
  for (i = 1; i < 4; i++) {
    if (!isZero(&buf[446 + i * 16], 16)) {
      warning("MBR partition %d is not empty", i + 1);
    }
  }
  if (buf[510] != 0x55 || buf[511] != 0xAA) {
    error("protective MBR has wrong signature");
  }
}


/**************************************************************/


void showPartTblHdr(unsigned char *buf,
                    unsigned int numSectors) {
  char signature[9];
  unsigned int revision;
  unsigned int hdrSize;
  unsigned int hdrCRC;
  unsigned int myLBAhi, myLBAlo;
  unsigned int altLBAhi, altLBAlo;
  unsigned int firstLBAhi, firstLBAlo;
  unsigned int lastLBAhi, lastLBAlo;
  uuid_t diskUUID;
  char diskUUIDstr[40];
  unsigned int ptblStrtLBAhi, ptblStrtLBAlo;
  unsigned int ptblNumEntries;
  unsigned int ptblEntrySize;
  unsigned int ptblCRC;
  unsigned int newCRC;

  /* show */
  printf("\nPartition table header\n");
  memset(signature, 0, 9);
  strncpy(signature, (char *) &buf[0], 8);
  printf("    signature        : '%s'\n", signature);
  revision = get4LE(&buf[8]);
  printf("    revision         : %u.%u\n",
         (revision >> 16) & 0x0000FFFF, revision & 0x0000FFFF);
  hdrSize = get4LE(&buf[12]);
  printf("    header size      : %u\n", hdrSize);
  hdrCRC = get4LE(&buf[16]);
  printf("    header CRC       : 0x%08X\n", hdrCRC);
  myLBAlo = get4LE(&buf[24]);
  myLBAhi = get4LE(&buf[28]);
  printf("    my LBA           : 0x%08X%08X\n", myLBAhi, myLBAlo);
  altLBAlo = get4LE(&buf[32]);
  altLBAhi = get4LE(&buf[36]);
  printf("    alternate LBA    : 0x%08X%08X\n", altLBAhi, altLBAlo);
  firstLBAlo = get4LE(&buf[40]);
  firstLBAhi = get4LE(&buf[44]);
  printf("    first usable LBA : 0x%08X%08X\n", firstLBAhi, firstLBAlo);
  lastLBAlo = get4LE(&buf[48]);
  lastLBAhi = get4LE(&buf[52]);
  printf("    last usable LBA  : 0x%08X%08X\n", lastLBAhi, lastLBAlo);
  uuid_copyLE(diskUUID, &buf[56]);
  uuid_unparse_upper(diskUUID, diskUUIDstr);
  printf("    disk UUID        : %s\n", diskUUIDstr);
  ptblStrtLBAlo = get4LE(&buf[72]);
  ptblStrtLBAhi = get4LE(&buf[76]);
  printf("    ptbl start LBA   : 0x%08X%08X\n", ptblStrtLBAhi, ptblStrtLBAlo);
  ptblNumEntries = get4LE(&buf[80]);
  printf("    ptbl num entries : %u\n", ptblNumEntries);
  ptblEntrySize = get4LE(&buf[84]);
  printf("    ptbl entry size  : %u\n", ptblEntrySize);
  ptblCRC = get4LE(&buf[88]);
  printf("    ptbl CRC         : 0x%08X\n", ptblCRC);
  /* check */
  if (strcmp(signature, "EFI PART") != 0) {
    error("partition table header has wrong signature");
  }
  if (revision != 0x00010000) {
    error("partition table header has wrong revision number");
  }
  if (hdrSize != 92) {
    error("partition table header has wrong size");
  }
  put4LE(&buf[16], 0x00000000);
  newCRC = crc32Sum(buf, 92);
  put4LE(&buf[16], hdrCRC);
  if (hdrCRC != newCRC) {
    error("partition table header has wrong CRC");
  }
  if (!isZero(&buf[20], 4)) {
    error("reserved bytes at offset 20 must be zero");
  }
  if (myLBAhi != 0 ||
      myLBAlo != 0x00000001) {
    error("this table header's LBA is wrong");
  }
  if (altLBAhi != 0 ||
      altLBAlo != numSectors - 1) {
    error("alternate table header's LBA is wrong");
  }
  if (firstLBAhi != 0 ||
      firstLBAlo != FIRST_USABLE_SECTOR) {
    warning("first usable LBA has an unexpected value");
  }
  if (lastLBAhi != 0 ||
      lastLBAlo != numSectors - 2 - NUMBER_PART_SECTORS) {
    warning("last usable LBA has an unexpected value");
  }
  /* note: disk UUID can only be cross-checked, done later */
  if (ptblStrtLBAhi != 0 ||
      ptblStrtLBAlo != 2) {
    error("partition table starts at wrong LBA");
  }
  if (ptblNumEntries != NUMBER_PART_ENTRIES) {
    error("wrong number of partition entries");
  }
  if (ptblEntrySize != SIZEOF_PART_ENTRY) {
    error("wrong size of partition entry");
  }
  /* note: ptbl CRC check done later, after reading the ptbl */
  if (!isZero(&buf[92], SECTOR_SIZE - 92)) {
    error("reserved part of header sector must be zero");
  }
}


void showBackupTblHdr(unsigned char *buf,
                      unsigned int numSectors) {
  char signature[9];
  unsigned int revision;
  unsigned int hdrSize;
  unsigned int hdrCRC;
  unsigned int myLBAhi, myLBAlo;
  unsigned int altLBAhi, altLBAlo;
  unsigned int firstLBAhi, firstLBAlo;
  unsigned int lastLBAhi, lastLBAlo;
  uuid_t diskUUID;
  char diskUUIDstr[40];
  unsigned int ptblStrtLBAhi, ptblStrtLBAlo;
  unsigned int ptblNumEntries;
  unsigned int ptblEntrySize;
  unsigned int ptblCRC;
  unsigned int newCRC;

  /* show */
  printf("\nBackup table header\n");
  memset(signature, 0, 9);
  strncpy(signature, (char *) &buf[0], 8);
  printf("    signature        : '%s'\n", signature);
  revision = get4LE(&buf[8]);
  printf("    revision         : %u.%u\n",
         (revision >> 16) & 0x0000FFFF, revision & 0x0000FFFF);
  hdrSize = get4LE(&buf[12]);
  printf("    header size      : %u\n", hdrSize);
  hdrCRC = get4LE(&buf[16]);
  printf("    header CRC       : 0x%08X\n", hdrCRC);
  myLBAlo = get4LE(&buf[24]);
  myLBAhi = get4LE(&buf[28]);
  printf("    my LBA           : 0x%08X%08X\n", myLBAhi, myLBAlo);
  altLBAlo = get4LE(&buf[32]);
  altLBAhi = get4LE(&buf[36]);
  printf("    alternate LBA    : 0x%08X%08X\n", altLBAhi, altLBAlo);
  firstLBAlo = get4LE(&buf[40]);
  firstLBAhi = get4LE(&buf[44]);
  printf("    first usable LBA : 0x%08X%08X\n", firstLBAhi, firstLBAlo);
  lastLBAlo = get4LE(&buf[48]);
  lastLBAhi = get4LE(&buf[52]);
  printf("    last usable LBA  : 0x%08X%08X\n", lastLBAhi, lastLBAlo);
  uuid_copyLE(diskUUID, &buf[56]);
  uuid_unparse_upper(diskUUID, diskUUIDstr);
  printf("    disk UUID        : %s\n", diskUUIDstr);
  ptblStrtLBAlo = get4LE(&buf[72]);
  ptblStrtLBAhi = get4LE(&buf[76]);
  printf("    ptbl start LBA   : 0x%08X%08X\n", ptblStrtLBAhi, ptblStrtLBAlo);
  ptblNumEntries = get4LE(&buf[80]);
  printf("    ptbl num entries : %u\n", ptblNumEntries);
  ptblEntrySize = get4LE(&buf[84]);
  printf("    ptbl entry size  : %u\n", ptblEntrySize);
  ptblCRC = get4LE(&buf[88]);
  printf("    ptbl CRC         : 0x%08X\n", ptblCRC);
  /* check */
  if (strcmp(signature, "EFI PART") != 0) {
    error("partition table header has wrong signature");
  }
  if (revision != 0x00010000) {
    error("partition table header has wrong revision number");
  }
  if (hdrSize != 92) {
    error("partition table header has wrong size");
  }
  put4LE(&buf[16], 0x00000000);
  newCRC = crc32Sum(buf, 92);
  put4LE(&buf[16], hdrCRC);
  if (hdrCRC != newCRC) {
    error("partition table header has wrong CRC");
  }
  if (get4LE(&buf[20]) != 0) {
    error("reserved bytes at offset 20 must be zero");
  }
  if (myLBAhi != 0 ||
      myLBAlo != numSectors - 1) {
    error("this table header's LBA is wrong");
  }
  if (altLBAhi != 0 ||
      altLBAlo != 0x00000001) {
    error("alternate table header's LBA is wrong");
  }
  if (firstLBAhi != 0 ||
      firstLBAlo != FIRST_USABLE_SECTOR) {
    warning("first usable LBA has an unexpected value");
  }
  if (lastLBAhi != 0 ||
      lastLBAlo != numSectors - 2 - NUMBER_PART_SECTORS) {
    warning("last usable LBA has an unexpected value");
  }
  /* note: disk UUID can only be cross-checked, done later */
  if (ptblStrtLBAhi != 0 ||
      ptblStrtLBAlo != numSectors - 1 - NUMBER_PART_SECTORS) {
    error("partition table starts at wrong LBA");
  }
  if (ptblNumEntries != NUMBER_PART_ENTRIES) {
    error("wrong number of partition entries");
  }
  if (ptblEntrySize != SIZEOF_PART_ENTRY) {
    error("wrong size of partition entry");
  }
  /* note: ptbl CRC check done later, after reading the ptbl */
  if (!isZero(&buf[92], SECTOR_SIZE - 92)) {
    error("reserved part of header sector must be zero");
  }
}


/**************************************************************/


void showPartTable(unsigned char *buf) {
  int i;
  bool empty;
  unsigned char *p;
  uuid_t typeUUID;
  char typeUUIDstr[40];
  uuid_t partUUID;
  char partUUIDstr[40];
  unsigned int startLBAhi, startLBAlo;
  unsigned int endLBAhi, endLBAlo;
  unsigned int attribHi, attribLo;
  int j;
  char c;
  char name[40];

  /* show */
  printf("\nPartition table\n");
  empty = true;
  for (i = 0; i < NUMBER_PART_ENTRIES; i++) {
    p = &buf[i * SIZEOF_PART_ENTRY];
    if (isZero(p, 16)) {
      /* not used */
      continue;
    }
    empty = false;
    uuid_copyLE(typeUUID, p + 0);
    uuid_unparse_upper(typeUUID, typeUUIDstr);
    uuid_copyLE(partUUID, p + 16);
    uuid_unparse_upper(partUUID, partUUIDstr);
    startLBAlo = get4LE(p + 32);
    startLBAhi = get4LE(p + 36);
    endLBAlo = get4LE(p + 40);
    endLBAhi = get4LE(p + 44);
    attribLo = get4LE(p + 48);
    attribHi = get4LE(p + 52);
    for (j = 0; j < 36; j++) {
      c = *(p + 56 + 2 * j);
      name[j] = c;
      if (c == 0) {
        break;
      }
    }
    printf("    partition %d:\n", i + 1);
    printf("        type UUID    : %s\n", typeUUIDstr);
    printf("        unique UUID  : %s\n", partUUIDstr);
    printf("        starting LBA : 0x%08X%08X\n", startLBAhi, startLBAlo);
    printf("        ending LBA   : 0x%08X%08X\n", endLBAhi, endLBAlo);
    printf("        attributes   : 0x%08X%08X\n", attribHi, attribLo);
    printf("        name         : '%s'\n", name);
  }
  if (empty) {
    printf("    -- no entries --\n");
  }
}


/**************************************************************/


int main(int argc, char *argv[]) {
  char *diskName;
  FILE *disk;
  unsigned long diskSize;
  unsigned int numSectors;
  unsigned char protMBR[SECTOR_SIZE];
  unsigned char partTblHdr[SECTOR_SIZE];
  unsigned char backupTblHdr[SECTOR_SIZE];
  unsigned int partTblCRC;
  unsigned int backupTblCRC;
  int s;
  unsigned char partTable[NUMBER_PART_BYTES];
  unsigned char backupTable[NUMBER_PART_BYTES];
  unsigned int partNewCRC;
  unsigned int backupNewCRC;

  /* check command line arguments */
  if (argc != 2) {
    printf("Usage: %s <disk image>\n", argv[0]);
    exit(1);
  }
  diskName = argv[1];
  /* initialize CRC32 table */
  crc32Init();
  /* open disk image */
  disk = fopen(diskName, "rb");
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
  /* show protective MBR */
  rdSector(disk, 0, protMBR);
  showProtectiveMBR(protMBR);
  /* show primary partition table header */
  rdSector(disk, 1, partTblHdr);
  showPartTblHdr(partTblHdr, numSectors);
  /* show backup partition table header */
  rdSector(disk, numSectors - 1, backupTblHdr);
  showBackupTblHdr(backupTblHdr, numSectors);
  /* check for identical disk UUIDs */
  if (memcmp(&partTblHdr[56], &backupTblHdr[56], sizeof(uuid_t)) != 0) {
    error("primary and backup headers have different disk UUIDs");
  }
  /* check for identical ptbl CRC values */
  partTblCRC = get4LE(&partTblHdr[88]);
  backupTblCRC = get4LE(&backupTblHdr[88]);
  if (partTblCRC != backupTblCRC) {
    error("primary and backup headers have different ptbl CRC values");
  }
  /* read primary partition table */
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    rdSector(disk, 2 + s,
             &partTable[s * SECTOR_SIZE]);
  }
  partNewCRC = crc32Sum(partTable, NUMBER_PART_BYTES);
  if (partNewCRC != partTblCRC) {
    error("primary ptbl CRC different from that stored in header");
  }
  /* read backup partition table */
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    rdSector(disk, numSectors - 1 - NUMBER_PART_SECTORS + s,
             &backupTable[s * SECTOR_SIZE]);
  }
  backupNewCRC = crc32Sum(backupTable, NUMBER_PART_BYTES);
  if (backupNewCRC != backupTblCRC) {
    error("backup ptbl CRC different from that stored in header");
  }
  /* show partition table */
  showPartTable(partTable);
  return 0;
}
