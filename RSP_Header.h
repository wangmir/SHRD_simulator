#ifndef __RSPHeader_H_
#define __RSPHeader_H_

typedef unsigned long long RSP_UINT64;
typedef unsigned long RSP_UINT32;
typedef unsigned short RSP_UINT16;
typedef unsigned char RSP_UINT8;

typedef bool RSP_BOOL;
typedef void RSP_VOID;

typedef RSP_UINT32 RSP_LPN;
typedef RSP_UINT16 RSP_SECTOR_BITMAP;

#define RSP_INVALID_LPN (0x07ffffff)
#define RSP_INVALID_RID (0xffffffff)
#define RSP_FULL_BITMAP (0xffff)

#define RSP_NUM_CHANNEL (4)
#define RSP_NUM_BANK (2)
#define RSP_NUM_PLANE (4)
#define RSP_PAGE_PER_WL (2)
#define RSP_SECTOR_PER_LPN (8)
#define RSP_SECTOR_PER_PAGE (16)
#define RSP_PAGE_PER_BLOCK (128)
#define RSP_BYTE_PER_SECTOR (512)
#define RSP_BLOCK_PER_PLANE (1987)

#define __COREID__ (0) //not used on the simulator

#endif