#ifndef __VFLWRAPPER_H__
#define __VFLWRAPPER_H__

#include "RSP_Header.h"
#include <stdio.h>

typedef struct ProgramOp
{
	RSP_UINT32* pData;
	RSP_UINT32* pSpareData;
	RSP_UINT8 nChannel;
	RSP_UINT8 nBank;
	RSP_UINT8 nPage;
	RSP_UINT16 nBlock;
	RSP_UINT16 bmpTargetSector;
	RSP_UINT32 m_anVPN[2]; 
	RSP_UINT32 m_anLPN[2];

}RSPProgramOp;

typedef struct ReadOp{
	RSP_UINT32* pData;
	RSP_UINT32* pSpareData;
	RSP_UINT32 nReqID;
	RSP_UINT16 bmpTargetSector;
	RSP_UINT8 nChannel;
	RSP_UINT8 nBank;
	RSP_UINT8 nPage;
	RSP_UINT16 nBlock;
	RSP_UINT32 m_nVPN;
	RSP_UINT32 m_nLPN;

}RSPReadOp;

typedef struct EraseOp{
	RSP_UINT8 nChannel;
	RSP_UINT8 nBank;
	RSP_UINT16 nBlock;
}RSPEraseOp;

class VFLWrapper
{
	public:


#define SMALL_DATA_BLOCK 1
#define VFL_BLOCK_DATA_SIZE (1048576LL)
#define VFL_SMALL_BLOCK_DATA_SIZE (1024LL)
#define VFL_BLOCK_OOB_SIZE (2048)

#define NUM_MAP_BLOCKS 5

		char dir[1024];
		char temp_dir[1024];

		RSP_UINT32 latest_sparedata[4];

		RSP_UINT32 profile[128];

		RSP_UINT32 CORE_ID;

		FILE *fp_data[2];
		FILE *fp_oob[2];

		RSP_UINT32 test_bit = 0;

		void *pHILWrapper;

		VFLWrapper(char *Working_dir, RSP_UINT32 CORE);

		void HIL_ptr(void *pHIL);

		void INC_PROGRAMPENDING();
		void WAIT_PROGRAMPENDING();
		void INC_ERASEPENDING();
		void WAIT_ERASEPENDING();
		void INC_READPENDING();
		void WAIT_READPENDING();
		void _GetSpareData(RSP_UINT32* spare_buf);
	
		RSP_UINT64 calc_seek_address(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);

		bool Issue(RSPProgramOp RSPOp[4]);
		bool Issue(RSPReadOp RSPOp);
		bool Issue(RSPEraseOp RSPOp[4]);

		bool test();

		bool MetaIssue(RSPProgramOp RSPOp[4]);
		bool MetaIssue(RSPReadOp RSPOp);

		bool RSP_SetProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData);
		bool RSP_INC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData);
		bool RSP_DEC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData);
};

#endif
