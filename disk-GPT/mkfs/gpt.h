/*
 * gpt.h -- GUID partition table
 */


#ifndef _GPT_H_
#define _GPT_H_


#define EOS32_FS	"2736CFB2-27C3-40C6-AC7A-40A7BE06476D"
#define EOS32_SWAP	"C1BD6361-342D-486E-ABBC-3547549A95F6"


void gptGetPartInfo(FILE *disk, unsigned int diskSize,
                    int partNumber, char *partType,
                    unsigned int *fsStart, unsigned int *fsSize);


#endif /* _GPT_H_ */
