#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "RSP_OSAL.h"

namespace RSPOSAL{

	void *RSP_MemAlloc(RSP_UINT32 type, RSP_UINT32 size){

		return malloc(size);
	}

	void RSP_MemSet(void *src, RSP_UINT32 val, RSP_UINT32 size){

		memset(src, val, size);
	}

	void RSP_MemCpy(void *dst, void *src, RSP_UINT32 size){

		memcpy(dst, src, size);
	}
	void RSP_BufferMemCpy(void *pstDestBuf, void *pstSrcBuf, RSP_UINT32 size){
		memcpy(pstDestBuf, pstSrcBuf, size);
	}


}