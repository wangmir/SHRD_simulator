#include <string.h>

#include "VFLWrapper.h"

char dir[1024];
FILE *

VFLWrapper::VFLWrapper(char * Working_dir){

	memcpy(dir, Working_dir, 1024);

}

bool VFLWrapper::Issue(RSPReadOp RSPOp){

}

bool VFLWrapper::Issue(RSPProgramOp RSPOp[4]){

}

bool VFLWrapper::Issue(RSPEraseOp RSPOp[4]){

}

bool VFLWrapper::MetaIssue(RSPProgramOp RSPOp[4]){

}

bool VFLWrapper::MetaIssue(RSPReadOp RSPOp){

}

bool VFLWrapper::RSP_SetProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){


}

bool VFLWrapper::RSP_INC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){


}

bool VFLWrapper::RSP_DEC_ProfileData(RSP_UINT32 idx, RSP_UINT32 ProfileData){


}

void VFLWrapper::_GetSpareData(RSP_UINT32 * spare_buf){

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