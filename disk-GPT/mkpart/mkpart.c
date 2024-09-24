/*
 * mkpart.c -- add a partition to a GUID partition table
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <uuid/uuid.h>

#include "parttypes.h"


#define SECTOR_SIZE		512
#define MIN_NUMBER_SECTORS	4096
#define SECTORS_PER_MB		((1 << 20) / SECTOR_SIZE)

#define NUMBER_PART_ENTRIES	128
#define SIZEOF_PART_ENTRY	128
#define NUMBER_PART_BYTES	(NUMBER_PART_ENTRIES * SIZEOF_PART_ENTRY)
#define NUMBER_PART_SECTORS	(NUMBER_PART_BYTES) / SECTOR_SIZE


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


void wrSector(FILE *disk, unsigned int sectorNum, unsigned char *buf) {
  if (fseek(disk, (unsigned long) sectorNum * SECTOR_SIZE, SEEK_SET) < 0) {
    error("cannot position to sector %u (0x%X)", sectorNum, sectorNum);
  }
  if (fwrite(buf, 1, SECTOR_SIZE, disk) != SECTOR_SIZE) {
    error("cannot write sector %u (0x%X)", sectorNum, sectorNum);
  }
}


/**************************************************************/


unsigned char primaryTblHdr[SECTOR_SIZE];
unsigned char primaryTable[NUMBER_PART_BYTES];

unsigned char backupTblHdr[SECTOR_SIZE];
unsigned char backupTable[NUMBER_PART_BYTES];


void checkProtMBR(FILE *disk) {
  unsigned char protMBR[SECTOR_SIZE];
  int i;

  rdSector(disk, 0, protMBR);
  if (protMBR[450] != 0xEE) {
    error("protective MBR has wrong OS type in partition 1");
  }
  for (i = 1; i < 4; i++) {
    if (!isZero(&protMBR[446 + i * 16], 16)) {
      warning("MBR partition %d is not empty", i + 1);
    }
  }
  if (protMBR[510] != 0x55 || protMBR[511] != 0xAA) {
    error("protective MBR has wrong signature");
  }
  printf("Protective MBR verified.\n");
}


void checkValidGPT(FILE *disk, unsigned int numSectors) {
  char signature[9];
  unsigned int oldHdrCRC;
  unsigned int newHdrCRC;
  unsigned int primaryLBAhi, primaryLBAlo;
  unsigned int backupLBAhi, backupLBAlo;
  int s;
  unsigned int oldTblCRC;
  unsigned int newTblCRC;
  unsigned int myLBAhi, myLBAlo;

  /* check protective MBR */
  checkProtMBR(disk);
  /* check primary table header */
  rdSector(disk, 1, primaryTblHdr);
  memset(signature, 0, 9);
  strncpy(signature, (char *) &primaryTblHdr[0], 8);
  if (strcmp(signature, "EFI PART") != 0) {
    error("primary table header has wrong signature");
  }
  oldHdrCRC = get4LE(&primaryTblHdr[16]);
  put4LE(&primaryTblHdr[16], 0x00000000);
  newHdrCRC = crc32Sum(primaryTblHdr, 92);
  put4LE(&primaryTblHdr[16], oldHdrCRC);
  if (oldHdrCRC != newHdrCRC) {
    error("primary table header has wrong CRC");
  }
  primaryLBAlo = get4LE(&primaryTblHdr[24]);
  primaryLBAhi = get4LE(&primaryTblHdr[28]);
  if (primaryLBAhi != 0 ||
      primaryLBAlo != 0x00000001) {
    error("primary table header's LBA is wrong");
  }
  backupLBAlo = get4LE(&primaryTblHdr[32]);
  backupLBAhi = get4LE(&primaryTblHdr[36]);
  if (backupLBAhi != 0 || backupLBAlo != numSectors - 1) {
    warning("backup table header is not located at end of disk");
  }
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    rdSector(disk, 2 + s,
             &primaryTable[s * SECTOR_SIZE]);
  }
  oldTblCRC = get4LE(&primaryTblHdr[88]);
  newTblCRC = crc32Sum(primaryTable, NUMBER_PART_BYTES);
  if (oldTblCRC != newTblCRC) {
    error("primary ptbl CRC different from that stored in header");
  }
  printf("Valid primary GPT verified.\n");
  /* check backup table header */
  rdSector(disk, backupLBAlo, backupTblHdr);
  memset(signature, 0, 9);
  strncpy(signature, (char *) &backupTblHdr[0], 8);
  if (strcmp(signature, "EFI PART") != 0) {
    error("backup table header has wrong signature");
  }
  oldHdrCRC = get4LE(&backupTblHdr[16]);
  put4LE(&backupTblHdr[16], 0x00000000);
  newHdrCRC = crc32Sum(backupTblHdr, 92);
  put4LE(&backupTblHdr[16], oldHdrCRC);
  if (oldHdrCRC != newHdrCRC) {
    error("backup table header has wrong CRC");
  }
  myLBAlo = get4LE(&backupTblHdr[24]);
  myLBAhi = get4LE(&backupTblHdr[28]);
  if (myLBAhi != backupLBAhi ||
      myLBAlo != backupLBAlo) {
    error("backup table header's LBA is wrong");
  }
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    rdSector(disk, backupLBAlo - NUMBER_PART_SECTORS + s,
             &backupTable[s * SECTOR_SIZE]);
  }
  oldTblCRC = get4LE(&backupTblHdr[88]);
  newTblCRC = crc32Sum(backupTable, NUMBER_PART_BYTES);
  if (oldTblCRC != newTblCRC) {
    error("backup ptbl CRC different from that stored in header");
  }
  printf("Valid backup GPT verified.\n");
}


void writeValidGPT(FILE *disk) {
  unsigned int crc;
  int s;
  unsigned int backupLBAlo;

  /* compute and store CRC of primary (and backup) table */
  crc = crc32Sum(primaryTable, NUMBER_PART_BYTES);
  put4LE(&primaryTblHdr[88], crc);
  put4LE(&backupTblHdr[88], crc);
  /* write primary table */
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    wrSector(disk, 2 + s,
             &primaryTable[s * SECTOR_SIZE]);
  }
  /* compute CRC of primary header, write primary header */
  put4LE(&primaryTblHdr[16], 0);
  crc = crc32Sum(primaryTblHdr, 92);
  put4LE(&primaryTblHdr[16], crc);
  wrSector(disk, 1, primaryTblHdr);
  printf("Primary GPT written.\n");
  /* write backup table (copy of primary table) */
  backupLBAlo = get4LE(&primaryTblHdr[32]);
  for (s = 0; s < NUMBER_PART_SECTORS; s++) {
    wrSector(disk, backupLBAlo - NUMBER_PART_SECTORS + s,
             &primaryTable[s * SECTOR_SIZE]);
  }
  /* compute CRC of backup header, write backup header */
  put4LE(&backupTblHdr[16], 0);
  crc = crc32Sum(backupTblHdr, 92);
  put4LE(&backupTblHdr[16], crc);
  wrSector(disk, backupLBAlo, backupTblHdr);
  printf("Backup GPT written.\n");
}


/**************************************************************/


bool debugGaps = false;


unsigned char sortedTable[NUMBER_PART_BYTES];
int sortedEntries;

struct {
  unsigned int addr;
  unsigned int size;
} gapTable[NUMBER_PART_ENTRIES + 1];
int numGaps;


int compare(const void *p1, const void *p2) {
  unsigned char *q1, *q2;
  unsigned int start1, start2;

  q1 = (unsigned char *) p1;
  q2 = (unsigned char *) p2;
  start1 = get4LE(q1 + 32);
  start2 = get4LE(q2 + 32);
  if (start1 < start2) {
    return -1;
  }
  if (start1 > start2) {
    return 1;
  }
  return 0;
}


void buildSortedTable(void) {
  int i;
  unsigned char *p, *q;

  /* copy non-empty slots from primaryTable to sortedTable */
  sortedEntries = 0;
  for (i = 0; i < NUMBER_PART_ENTRIES; i++) {
    p = &primaryTable[i * SIZEOF_PART_ENTRY];
    if (!isZero(p, 16)) {
      q = &sortedTable[sortedEntries * SIZEOF_PART_ENTRY];
      memcpy(q, p, SIZEOF_PART_ENTRY);
      sortedEntries++;
    }
  }
  /* sort all entries in sortedTable */
  qsort(sortedTable, sortedEntries, SIZEOF_PART_ENTRY, compare);
}


void recordGaps(unsigned int firstSector, unsigned int lastSector) {
  unsigned int prevTop;
  unsigned int currBase;
  unsigned int gapSize;
  int i;
  unsigned char *p;

  numGaps = 0;
  prevTop = firstSector;
  for (i = 0; i < sortedEntries; i++) {
    p = &sortedTable[i * SIZEOF_PART_ENTRY];
    currBase = get4LE(p + 32);
    if (currBase < prevTop) {
      /* overlapping partitions */
      error("overlapping partitions");
    }
    gapSize = currBase - prevTop;
    if (gapSize != 0) {
      /* gap between patitions detected */
      gapTable[numGaps].addr = prevTop;
      gapTable[numGaps].size = gapSize;
      numGaps++;
    }
    prevTop = get4LE(p + 40) + 1;
  }
  currBase = lastSector + 1;
  if (currBase < prevTop) {
    /* topmost partition is too big */
    error("topmost partition is too big for disk");
  }
  gapSize = currBase - prevTop;
  if (gapSize != 0) {
    /* gap at end of topmost partition detected */
    gapTable[numGaps].addr = prevTop;
    gapTable[numGaps].size = gapSize;
    numGaps++;
  }
}


void showGaps(void) {
  int i;

  printf("Gaps:\n");
  for (i = 0; i < numGaps; i++) {
    printf("addr = 0x%08X, size = 0x%08X\n",
           gapTable[i].addr, gapTable[i].size);
  }
}


void buildGapTable(unsigned int firstSector, unsigned int lastSector) {
  buildSortedTable();
  recordGaps(firstSector, lastSector);
  if (debugGaps) {
    showGaps();
  }
}


unsigned int findGap(unsigned int size) {
  int i;

  for (i = 0; i < numGaps; i++) {
    if (gapTable[i].size >= size) {
      /* space is big enough */
      return gapTable[i].addr;
    }
  }
  /* no place found */
  return 0;
}


bool checkGap(unsigned int addr, unsigned int size) {
  int i;
  unsigned int base, top;

  for (i = 0; i < numGaps; i++) {
    base = gapTable[i].addr;
    top = base + gapTable[i].size;
    if (addr >= base && addr < top) {
      /* requested addr falls into this gap */
      if (top - addr >= size) {
        /* remaining space is big enough */
        return true;
      }
      /* remaining space is too small */
      return false;
    }
  }
  /* requested address doesn't even fall into any gap */
  return false;
}


/**************************************************************/


void mkPartition(FILE *disk,
                 int partNumber,
                 char *partCode,
                 unsigned int partStart,
                 unsigned int partSize,
                 unsigned int firstSector,
                 unsigned int lastSector) {
  int i;
  unsigned char *p;
  PartType *q;
  uuid_t typeUUID;
  uuid_t partUUID;
  char c;

  if (partNumber == 0) {
    /* search for a free slot */
    for (i = 0; i < NUMBER_PART_ENTRIES; i++) {
      p = &primaryTable[i * SIZEOF_PART_ENTRY];
      if (isZero(p, 16)) {
        break;
      }
    }
    if (i == NUMBER_PART_ENTRIES) {
      error("no currently unused partition found");
    }
    partNumber = i + 1;
  } else {
    /* check if the requested slot is free */
    p = &primaryTable[(partNumber - 1) * SIZEOF_PART_ENTRY];
    if (!isZero(p, 16)) {
      error("partition %d is currently in use", partNumber);
    }
  }
  /* lookup type code in data base */
  for (i = 0; i < sizeof(partTypes)/sizeof(partTypes[0]); i++) {
    q = &partTypes[i];
    if (strcmp(q->code, partCode) == 0) {
      break;
    }
  }
  if (i == sizeof(partTypes)/sizeof(partTypes[0])) {
    error("partition type code '%s' not found in list", partCode);
  }
  /* search for (or verify) a gap where the partition can live */
  buildGapTable(firstSector, lastSector);
  if (partStart == 0) {
    /* search for a start sector with enough space following */
    partStart = findGap(partSize);
    if (partStart == 0) {
      error("cannot find a gap which is big enough");
    }
  } else {
    /* check if enough space is following the requested start sector */
    if (!checkGap(partStart, partSize)) {
      error("the given start sector is not followed by enough space");
    }
  }
  /* clear entry */
  memset(p, 0, SIZEOF_PART_ENTRY);
  /* fill entry */
  uuid_parse(q->uuidStr, typeUUID);
  uuid_copyLE(p + 0, typeUUID);
  uuid_generate(partUUID);
  uuid_copyLE(p + 16, partUUID);
  put4LE(p + 32, partStart);
  put4LE(p + 40, partStart + partSize - 1);
  for (i = 0; i < 35; i++) {
    c = q->name[i];
    *(p + 56 + 2 * i) = c;
    if (c == 0) {
      break;
    }
  }
  printf("Partition %d created.\n", partNumber);
}


/**************************************************************/


void listPartTypes(void) {
  int i;

  printf("Type    Name\n");
  for (i = 0; i < sizeof(partTypes)/sizeof(partTypes[0]); i++) {
    printf("%s    %s\n", partTypes[i].code, partTypes[i].name);
  }
}


int main(int argc, char *argv[]) {
  char *diskName;
  char *partCode;
  unsigned int partSize;
  int partNumber;
  unsigned int partStart;
  char *endptr;
  FILE *disk;
  unsigned long diskSize;
  unsigned int numSectors;
  unsigned int firstSector;
  unsigned int lastSector;

  /* check command line arguments */
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    /* special case: print list of partition type codes */
    listPartTypes();
    exit(0);
  }
  if (argc < 4 || argc > 6) {
    printf("Usage:\n"
           "    %s --list\n"
           "        show the list of available partition type codes\n"
           "    %s <disk> <code> <size>[M] [<part> [<start>]]\n"
           "        add a new partition table entry with:\n"
           "        <disk>  disk image file\n"
           "        <code>  partition type code\n"
           "                (for a list see '%s --list' above)\n"
           "        <size>  partition size in number of sectors\n"
           "                (if 'M' appended: MiB instead of sectors)\n"
           "        <part>  optional partition number\n"
           "                (0: search for a free slot)\n"
           "        <start> optional partition start sector\n"
           "                (0: search for a place big enough)\n",
           argv[0], argv[0], argv[0]);
    exit(1);
  }
  diskName = argv[1];
  partCode = argv[2];
  partSize = strtoul(argv[3], &endptr, 0);
  if (*endptr == 'M') {
    partSize *= SECTORS_PER_MB;
    endptr++;
  }
  if (*endptr != '\0') {
    error("cannot read partition size");
  }
  partNumber = 0;
  if (argc > 4) {
    partNumber = strtoul(argv[4], &endptr, 0);
    if (*endptr != '\0') {
      error("cannot read partition number");
    }
    if (partNumber < 0 || partNumber > NUMBER_PART_ENTRIES) {
      error("partition number must be in range %d..%d (inclusive), or 0",
            1, NUMBER_PART_ENTRIES);
    }
  }
  partStart = 0;
  if (argc > 5) {
    partStart = strtoul(argv[5], &endptr, 0);
    if (*endptr != '\0') {
      error("cannot read partition start sector");
    }
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
  checkValidGPT(disk, numSectors);
  firstSector = get4LE(&primaryTblHdr[40]);
  lastSector = get4LE(&primaryTblHdr[48]);
  mkPartition(disk, partNumber, partCode,
              partStart, partSize,
              firstSector, lastSector);
  writeValidGPT(disk);
  return 0;
}
