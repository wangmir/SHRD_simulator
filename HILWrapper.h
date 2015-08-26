#ifndef __HILWrapper_H__
#define __HILWrapper_H__

#include "RSP_header.h"
#include "ATLWrapper.h"

using namespace Hesper;

#define TOTAL_BUFF_SIZE_IN_MB (20)
#define BUFFER_SIZE_IN_KB (8)
#define NUM_BUFF (TOTAL_BUFF_SIZE_IN_MB * MB / (BUFFER_SIZE_IN_KB * KB))
#define NUM_PERBANK_QUEUE (RSP_NUM_BANK * RSP_NUM_CHANNEL)
#define BUFF_THRESHOLD 512
#define SPECIAL_LPN_START 14397440

#define WRITE 0 
#define READ 1


//make HIL layer 8KB buffer 
struct HIL_buff{
	RSP_LPN LPN[2];
	RSP_SECTOR_BITMAP BITMAP;
	RSP_UINT32 RW; //0 is write, 1 is read
	RSP_UINT8 partial; //if 0, need to alloc new buff, if 1 then write to latter 4KB
	RSP_UINT32 *buff;
	HIL_buff *next;
	HIL_buff *before;
};

struct HIL_queue{
	HIL_buff *list; //head out tail in
	RSP_UINT32 size;
};

class HILWrapper{

public:

	ATLWrapper *pATLWrapper[2];
	HIL_queue *bank_queue[2]; //per bank queue
	HIL_queue urgent_queue[2];
	HIL_queue free_buff_queue;
	RSP_UINT32 on_going_request;
	RSP_UINT32 on_going_urgent_request;
	RSP_UINT32 on_going_normal_request;

	RSP_UINT32 global_rr_cnt;	//for round robin handling, 
								//choose start bank number by this counter

	HILWrapper(ATLWrapper *ATL0, ATLWrapper *ATL1);

	RSP_BOOL HIL_WriteLPN(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32 *buff);
	RSP_BOOL HIL_ReadLPN(RSP_UINT32 RID, RSP_LPN lpn, RSP_SECTOR_BITMAP SetorBitmap, RSP_UINT32 *buff);
};

#endif