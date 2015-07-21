#ifndef __RSP_OSAL_H__
#define __RSP_OSAL_H__

#include "RSP_Header.h"

namespace RSPOSAL
{
	enum{
		DRAM,
		SRAM
	};

	void* RSP_MemAlloc(RSP_UINT32 type, RSP_UINT32 size);
	void RSP_MemSet(void* src, RSP_UINT32 val, RSP_UINT32 size);
	void RSP_MemCpy(void* dst, void* src, RSP_UINT32 size);

	void RSP_BufferMemCpy(void *pstDestBuf, void *pstSrcBuf, RSP_UINT32 size);

}

#endif
