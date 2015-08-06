#include <string.h>
#include <stdio.h>
#include <direct.h>

#include "VFLWrapper.h"
#include "ATLWrapper.h"

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
	
	for (int i = 0; i < NUM_FTL_CORE; i++){

		sprintf(temp, "%s/core_%d", dir, i);
		_mkdir(temp);

		for (int j = 0; j < RSP_NUM_CHANNEL; j++){

			sprintf(temp, "%s/core_%d/channel_%d", dir, i, j);
			_mkdir(temp);

			for (int k = 0; k < RSP_NUM_BANK; k++){

				sprintf(temp, "%s/core_%d/channel_%d/bank_%d", dir, i, j, k);
				_mkdir(temp);
			}
		}
	}
}

//READ
bool VFLWrapper::Issue(RSPReadOp RSPOp){

	sprintf(temp_dir, "%s/core_%d/channel_%d/bank_%d/blk_data_%d", dir, CORE_ID, RSPOp.nChannel, RSPOp.nBank, RSPOp.nBlock);
	FILE *fp_data = fopen(temp_dir, "rb");
	RSP_ASSERT(fp_data);

	sprintf(temp_dir, "%s/core_%d/channel_%d/bank_%d/blk_oob_%d", dir, CORE_ID, RSPOp.nChannel, RSPOp.nBank, RSPOp.nBlock);
	FILE *fp_oob = fopen(temp_dir, "rb");
	RSP_ASSERT(fp_oob);

	//superpage aligned block file
	//offset means lpn (4kb) offset on the superblock (plane * block)
	RSP_UINT32 offset = RSPOp.nPage * LPAGE_PER_PPAGE;

	if (RSPOp.bmpTargetSector == 0xff00){
		offset++;
	}

	fseek(fp_data, offset * RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN, SEEK_SET);
	fread((char *)RSPOp.pData, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN, 1, fp_data);

	//2 means o_addr and t_addr pair on the spare area
	fseek(fp_oob, offset * 2, SEEK_SET);
	
	if (RSPOp.bmpTargetSector == 0xff){
		fread((char *)latest_sparedata, sizeof(RSP_UINT32) * 2, 1, fp_oob);
	}
	else {
		fread((char *)&latest_sparedata[2], sizeof(RSP_UINT32) * 2, 1, fp_oob);
	}

	fclose(fp_data);
	fclose(fp_oob);

	return true;
}

//WRITE
bool VFLWrapper::Issue(RSPProgramOp RSPOp[4]){

	for (RSP_UINT32 plane = 0; plane < PLANES_PER_BANK; plane++){

		sprintf(temp_dir, "%s/core_%d/channel_%d/bank_%d/blk_data_%d", dir, CORE_ID, RSPOp[plane].nChannel, RSPOp[plane].nBank, RSPOp[plane].nBlock);
		FILE *fp_data = fopen(temp_dir, "wb+");
		RSP_ASSERT(fp_data);

		sprintf(temp_dir, "%s/core_%d/channel_%d/bank_%d/blk_oob_%d", dir, CORE_ID, RSPOp[plane].nChannel, RSPOp[plane].nBank, RSPOp[plane].nBlock);
		FILE *fp_oob = fopen(temp_dir, "wb+");
		RSP_ASSERT(fp_oob);

		//superpage aligned block file
		//offset means lpn (4kb) offset on the superblock (plane * block)
		RSP_UINT32 offset = RSPOp[plane].nPage;

		fseek(fp_data, offset * RSP_BYTES_PER_PAGE, SEEK_SET);
		fwrite((char *) RSPOp[plane].pData, RSP_BYTES_PER_PAGE, 1, fp_data);

		//2 means o_addr and t_addr pair on the spare area
		fseek(fp_oob, offset * 2 * LPAGE_PER_PPAGE, SEEK_SET);
		fwrite((char *) RSPOp[plane].pSpareData, sizeof(RSP_UINT32) * 2 * LPAGE_PER_PPAGE, 1, fp_oob);

		fclose(fp_data);
		fclose(fp_oob);

	}
	return true;

}

//ERASE
bool VFLWrapper::Issue(RSPEraseOp RSPOp[4]){

	for (RSP_UINT32 plane = 0; plane < PLANES_PER_BANK; plane++){

		RSP_UINT32 ret;
		sprintf(temp_dir, "%s/core_%d/channel_%d/bank_%d/blk_data_%d", dir, CORE_ID, RSPOp[plane].nChannel, RSPOp[plane].nBank, RSPOp[plane].nBlock);
		ret = remove(temp_dir);

		//just warn because at the init, they erase empty block
		if (ret == -1)
			printf("WARNING: ERASE FAILED\n");
	}
	return true;
}

bool VFLWrapper::MetaIssue(RSPProgramOp RSPOp[4]){

	return Issue(RSPOp);

}

bool VFLWrapper::MetaIssue(RSPReadOp RSPOp){

	return Issue(RSPOp);
}

void VFLWrapper::_GetSpareData(RSP_UINT32 * spare_buf){

	RSPOSAL::RSP_MemCpy(spare_buf, latest_sparedata, 4 * sizeof_u32);
}

bool VFLWrapper::RSP_SetProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

	return true;
}

bool VFLWrapper::RSP_INC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

	return true;
}

bool VFLWrapper::RSP_DEC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){

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