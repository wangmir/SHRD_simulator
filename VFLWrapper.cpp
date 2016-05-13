#include <string.h>
#include <stdio.h>
#include <direct.h>
#include <stdlib.h>

#include "VFLWrapper.h"
#include "ATLWrapper.h"
#include "HILWrapper.h"

/*	file name convention 
	/-core-/-channel-/-bank-/-block-/
	ex) dir/core_folder/channel_folder/bank_folder/data_blockno or oob_blockno

	in_block file align
	32KB page level align
	/plane 0-page 0-lpage-0/plane 0-page 0-lpage 1/plane 1-page 0-lpage 0/plane 1-page 0-lpage 1/ ...

	oob size is: two u32 per lpage
*/

VFLWrapper::VFLWrapper(char * Working_dir, RSP_UINT32 CORE){

	char temp[1024];
	memset(dir, 0x00, 1024);
	memset(temp, 0x00, 1024);
	memcpy(dir, Working_dir, 1024);

	CORE_ID = CORE;
	
	for (int i = 0; i < NUM_FTL_CORE; i++) {
		sprintf(temp_dir, "%s/core_%d_data", dir, i);
		fp_data[i] = fopen(temp_dir, "w+b");
		sprintf(temp_dir, "%s/core_%d_oob", dir, i);
		fp_oob[i] = fopen(temp_dir, "w+b");
	}
}

void VFLWrapper::HIL_ptr(void *pHIL){
	pHILWrapper = pHIL;
}

//READ
bool VFLWrapper::Issue(RSPReadOp RSPOp){

	RSP_UINT32 seek = 0;
	
	seek += RSPOp.nChannel * (BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE);
	seek += RSPOp.nBank * (PLANES_PER_BANK * BLKS_PER_PLANE);
	seek += RSPOp.nBlock;

	RSP_UINT32 offset = RSPOp.nPage * LPAGE_PER_PPAGE;

	if (RSPOp.bmpTargetSector == 0xff00){
		offset++;
	}

	_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_SECTOR_PER_LPN * RSP_BYTE_PER_SECTOR, SEEK_SET);
	fread((char *)RSPOp.pData, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN, 1, fp_data[CORE_ID]);

	//2 means o_addr and t_addr pair on the spare area
	fseek(fp_oob[CORE_ID], seek * VFL_BLOCK_OOB_SIZE + offset * 2 * sizeof(RSP_UINT32), SEEK_SET);
	
	if (RSPOp.bmpTargetSector == 0xff){
		fread((char *)latest_sparedata, sizeof(RSP_UINT32) * 2, 1, fp_oob[CORE_ID]);
	}
	else {
		fread((char *)&latest_sparedata[2], sizeof(RSP_UINT32) * 2, 1, fp_oob[CORE_ID]);
	}

	return true;
}

//WRITE
bool VFLWrapper::Issue(RSPProgramOp RSPOp[4]){

	HILWrapper *HIL = (HILWrapper *) pHILWrapper;

	for (RSP_UINT32 plane = 0; plane < PLANES_PER_BANK; plane++){

		if (RSPOp[plane].pData == NULL)
			continue;

		RSP_UINT64 seek = 0;

		seek += RSPOp[plane].nChannel * (BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE);
		seek += RSPOp[plane].nBank * (PLANES_PER_BANK * BLKS_PER_PLANE);
		seek += RSPOp[plane].nBlock;

		RSP_UINT32 offset = RSPOp[plane].nPage;

		_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_BYTES_PER_PAGE, SEEK_SET);
		fwrite((char *) RSPOp[plane].pData, RSP_BYTES_PER_PAGE, 1, fp_data[CORE_ID]);

		_fseeki64(fp_oob[CORE_ID], seek * VFL_BLOCK_OOB_SIZE + offset * 2 * LPAGE_PER_PPAGE * sizeof(RSP_UINT32), SEEK_SET);
		fwrite((char *) RSPOp[plane].pSpareData, sizeof(RSP_UINT32) * 2 * LPAGE_PER_PPAGE, 1, fp_oob[CORE_ID]);

		HIL->HIL_BuffFree(RSPOp[plane].pData);

	}

	return true;

}

//ERASE
bool VFLWrapper::Issue(RSPEraseOp RSPOp[4]){

	for (RSP_UINT32 plane = 0; plane < PLANES_PER_BANK; plane++){

		RSP_UINT32 ret;
	}
	return true;
}

bool VFLWrapper::MetaIssue(RSPProgramOp RSPOp[4]){

	for (RSP_UINT32 plane = 0; plane < PLANES_PER_BANK; plane++){

		if (RSPOp[plane].pData == NULL)
			continue;

		RSP_UINT64 seek = 0;

		seek += RSPOp[plane].nChannel * (BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE);
		seek += RSPOp[plane].nBank * (PLANES_PER_BANK * BLKS_PER_PLANE);
		seek += RSPOp[plane].nBlock;

		RSP_UINT32 offset = RSPOp[plane].nPage;

		_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_BYTES_PER_PAGE, SEEK_SET);
		fwrite((char *) RSPOp[plane].pData, RSP_BYTES_PER_PAGE, 1, fp_data[CORE_ID]);

		_fseeki64(fp_oob[CORE_ID], seek * VFL_BLOCK_OOB_SIZE + offset * 2 * LPAGE_PER_PPAGE * sizeof(RSP_UINT32), SEEK_SET);
		fwrite((char *)RSPOp[plane].pSpareData, sizeof(RSP_UINT32) * 2 * LPAGE_PER_PPAGE, 1, fp_oob[CORE_ID]);

	}

	return true;

}

//METAISSUE read performs with 8KB page (not 4KB LPAGE)
bool VFLWrapper::MetaIssue(RSPReadOp RSPOp){

	RSP_UINT64 seek = 0;

	seek += RSPOp.nChannel * (BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE);
	seek += RSPOp.nBank * (PLANES_PER_BANK * BLKS_PER_PLANE);
	seek += RSPOp.nBlock;

	
	RSP_UINT32 offset = RSPOp.nPage;
	RSP_UINT32 ret = 0;


	_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_BYTES_PER_PAGE, SEEK_SET);
	ret = fread((char *) RSPOp.pData, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_PAGE, 1, fp_data[CORE_ID]);
	if (ret == 0)
		perror("Read MetaIssue");
	_fseeki64(fp_oob[CORE_ID], seek * VFL_BLOCK_OOB_SIZE + offset * 2 * LPAGE_PER_PPAGE * sizeof(RSP_UINT32), SEEK_SET);
	ret = fread((char *)latest_sparedata, sizeof(RSP_UINT32) * 2 * LPAGE_PER_PPAGE, 1, fp_oob[CORE_ID]);
	if (ret == 0)
		perror("Read MetaIssue");
	return true;
}

void VFLWrapper::_GetSpareData(RSP_UINT32 * spare_buf){

	RSPOSAL::RSP_MemCpy(spare_buf, latest_sparedata, 4 * sizeof_u32);
}

bool VFLWrapper::RSP_SetProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

	profile[idx] = ProfileData;

	return true;
}

bool VFLWrapper::RSP_INC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

	profile[idx] += ProfileData;

	return true;
}

bool VFLWrapper::RSP_DEC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

	profile[idx] -= ProfileData;

	return true;
}

void VFLWrapper::WAIT_READPENDING(){

}

void VFLWrapper::WAIT_PROGRAMPENDING(){

}

void VFLWrapper::WAIT_ERASEPENDING(){

}

void VFLWrapper::INC_PROGRAMPENDING(){

}

void VFLWrapper::INC_READPENDING(){

}

void VFLWrapper::INC_ERASEPENDING(){

}

bool VFLWrapper::test() {
	RSP_UINT64 seek = 0;
	RSP_UINT32 ret = 0;
	RSP_UINT32 *buff = (RSP_UINT32 *)malloc(8192);

	seek = 2274;


	RSP_UINT32 offset = 15;
	_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_BYTES_PER_PAGE, SEEK_SET);
	ret = fread((char *)buff, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_PAGE, 1, fp_data[CORE_ID]);

	if (*buff != 0x88888888)
		printf("!!");

	memset(buff, 0x00, 8192);

	offset = 30;
	_fseeki64(fp_data[CORE_ID], seek * VFL_BLOCK_DATA_SIZE + offset * RSP_SECTOR_PER_LPN * RSP_BYTE_PER_SECTOR, SEEK_SET);
	ret = fread((char *)buff, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN, 1, fp_data[CORE_ID]);

	if (*buff != 0x88888888)
		printf("!!");

	free(buff);
	return true;
}