#ifndef __crc32_h__
#define __crc32_h__

void initcrc32table(void);

unsigned long crc32(unsigned char *blk, unsigned long len);

void crc32init(unsigned long *crcvar);
void crc32block(unsigned long *crcvar, unsigned char *blk, unsigned long len);
unsigned long crc32finish(unsigned long *crcvar);

#endif