#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "HILWrapper.h"
#include "ATLWrapper.h"

using namespace Hesper;

static void _ASSERT(bool test){

	if (test){

		printf("HIL ASSERT");
		while (1){

			RSP_UINT32 i = 0;
		}
	}

}

static void insert_buff(HIL_queue *q, HIL_buff *buff){

	if (!buff){
		_ASSERT(1);
	}

	if (q->size == 0){

		q->list = buff;
		q->list->next = q->list;
		q->list->before = q->list;
	}
	else{
		buff->before = q->list->before;
		buff->next = q->list;
		q->list->before->next = buff;
		q->list->before = buff;
	}
	q->size++;

}

static void del_buff(HIL_queue *q, HIL_buff *buff){

	if (!buff)
		_ASSERT(1);

	if (q->list == buff)
	{
		q->list = buff->next;
	}
	buff->before->next = buff->next;
	buff->next->before = buff->before;
	q->size--;
	if (q->size == 0)
		q->list = NULL;
}

static void *add_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
{
	return (void *) ((RSP_UINT32) start_addr + offset);
}
static void *sub_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
{
	return (void *) ((RSP_UINT32) start_addr - offset);
}

static void HIL_buff_init(HIL_buff *buff){

	buff->LPN[0] = RSP_INVALID_LPN;
	buff->LPN[1] = RSP_INVALID_LPN;

	memset(buff->buff, 0x00, 8192);

	buff->BITMAP = 0;
}

HILWrapper::HILWrapper(ATLWrapper *ATL0, ATLWrapper *ATL1){

	pATLWrapper[0] = ATL0;
	pATLWrapper[1] = ATL1;

	//memory initialization

	HIL_buff *buff = (HIL_buff*) malloc(sizeof(HIL_buff) * NUM_BUFF);

	memset(buff, 0x00, sizeof(HIL_buff) * NUM_BUFF);

	for (RSP_UINT32 iter = 0; iter < NUM_BUFF; iter++){
		buff[iter].buff = (RSP_UINT32 *) malloc(BUFFER_SIZE_IN_KB * KB);
		buff[iter].LPN[0] = RSP_INVALID_LPN;
		buff[iter].LPN[1] = RSP_INVALID_LPN;
		insert_buff(&free_buff_queue, &buff[iter]);
	}

	bank_queue[0] = (HIL_queue *) malloc(sizeof(HIL_queue) * NUM_PERBANK_QUEUE);
	bank_queue[1] = (HIL_queue *) malloc(sizeof(HIL_queue) * NUM_PERBANK_QUEUE);

	memset(bank_queue[0], 0x00, sizeof(HIL_queue) * NUM_PERBANK_QUEUE);
	memset(bank_queue[1], 0x00, sizeof(HIL_queue) * NUM_PERBANK_QUEUE);

	on_going_request = 0;
	on_going_urgent_request = 0;
	on_going_normal_request = 0;
	global_rr_cnt = 0;
}

RSP_BOOL HILWrapper::HIL_WriteLPN(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32 *buff){

	RSP_UINT32 core = lpn % NUM_FTL_CORE;
	RSP_UINT32 incore_lpn = lpn / NUM_FTL_CORE;

	if (incore_lpn >= JN_LOG_START_IN_PAGE){

		//urgent requests
		HIL_buff *temp = NULL;

		if (free_buff_queue.size > 0){

			temp = free_buff_queue.list;
			del_buff(&free_buff_queue, temp);

			memcpy(temp->buff, buff, 4096);
			temp->LPN[0] = lpn;
			temp->BITMAP = 0xff & SectorBitmap;

			temp->RW = WRITE;

			insert_buff(&urgent_queue[core], temp);
			on_going_urgent_request++;

		}
		else
			_ASSERT(1);
	}
	else {
		RSP_UINT32 bank = incore_lpn % (RSP_NUM_CHANNEL * RSP_NUM_BANK);

		HIL_buff *temp = NULL;

		if ((bank_queue[core][bank].list != NULL) && (bank_queue[core][bank].list->before->partial)){
			//queue has partial buffer, use it
			temp = bank_queue[core][bank].list->before;

			memcpy((RSP_UINT32 *) add_addr(temp->buff, 4096), buff, 4096);
			temp->LPN[1] = lpn;
			temp->BITMAP |= 0xff00 & (SectorBitmap << 8);
			temp->RW = WRITE;

			temp->partial = 0;
		}

		else{

			temp = free_buff_queue.list;
			del_buff(&free_buff_queue, temp);

			memcpy(temp->buff, buff, 4096);
			temp->LPN[0] = lpn;
			temp->BITMAP = 0xff & SectorBitmap;
			temp->RW = WRITE;
			
			insert_buff(&bank_queue[core][bank], temp);
			temp->partial = 1;
		}

		on_going_normal_request++;
	}

	on_going_request++;

	if (on_going_request >= BUFF_THRESHOLD){
		//fetch request into FTL

		//always send two requests

		HIL_buff *buffptr0 = NULL, *buffptr1 = NULL;

		if (on_going_urgent_request){
			//need to handle urgent request

			if (urgent_queue[0].size){

				buffptr0 = urgent_queue[0].list;

				pATLWrapper[0]->RSP_WritePage(buffptr0->LPN, buffptr0->BITMAP, buffptr0->buff);

				del_buff(&urgent_queue[0], buffptr0);
				HIL_buff_init(buffptr0);
				insert_buff(&free_buff_queue, buffptr0);
				on_going_urgent_request--;

			}

			if (urgent_queue[1].size){

				buffptr1 = urgent_queue[1].list;

				pATLWrapper[1]->RSP_WritePage(buffptr1->LPN, buffptr1->BITMAP, buffptr1->buff);

				del_buff(&urgent_queue[1], buffptr1);
				HIL_buff_init(buffptr1);
				insert_buff(&free_buff_queue, buffptr1);
				on_going_urgent_request--;
			}
		}
		else {
			//pick with round robin from normal bank queue

			HIL_buff *buffptr[2] = { NULL, NULL };

			for (RSP_UINT32 core_iter = 0; core_iter < NUM_FTL_CORE; core_iter++){

				for (RSP_UINT32 bank_iter = 0; bank_iter < NUM_PERBANK_QUEUE; bank_iter++){

					RSP_UINT32 bank_no = (global_rr_cnt + bank_iter) % NUM_PERBANK_QUEUE;

					if (bank_queue[core_iter][bank_no].size){

						buffptr[core_iter] = bank_queue[core_iter][bank_no].list;

						pATLWrapper[core_iter]->RSP_WritePage(buffptr[core_iter]->LPN, buffptr[core_iter]->BITMAP, buffptr[core_iter]->buff);

						del_buff(&bank_queue[core_iter][bank_no], buffptr[core_iter]);
						HIL_buff_init(buffptr[core_iter]);
						insert_buff(&free_buff_queue, buffptr[core_iter]);
						on_going_normal_request--;

						break;
					}
				}
			}
		}
	}
	
	return true;	
}

RSP_BOOL HILWrapper::HIL_ReadLPN(RSP_UINT32 RID, RSP_LPN lpn, RSP_SECTOR_BITMAP SetorBitmap, RSP_UINT32 *buff){

	return true;
}

