#include <cstddef>
#include <new>

#include "RSP_Header.h"

#include "RSP_OSAL.h"

#include "VFLWrapper.h"
#include "ATLWrapper.h"

//for debugging
RSP_UINT32 dbg1;
RSP_UINT32 dbg2;
RSP_UINT32 dbg3;
RSP_UINT32 dbg4;
RSP_UINT32 dbg5;
RSP_UINT32 dbg6;
RSP_UINT32 dbglpn0;
RSP_UINT32 dbglpn1;

RSP_UINT32 dbg_iter1;
RSP_UINT32 dbg_iter2;

RSP_UINT32 dbg_twrite_hdr_cnt;
RSP_UINT32 dbg_twrite_data_cnt;
RSP_UINT32 dbg_remap_cnt;

RSP_UINT32 *dbgbuff;
RSP_UINT32 *dbgbuff_cpy;
//RSP_UINT32 **dbg_buffflush;
//RSP_UINT32 dbg_buffflush_ptr; 

struct LOG_DBG_ENTRY{

	RSP_UINT8 cmd; //0: SW, 1: RW, 2: JN, 3: Twrite header, 4: remap
	RSP_UINT32 buff_addr;
	RSP_UINT32 LPN[2];
	RSP_UINT32 loop;
	RSP_UINT32 tf; //true or false

};

LOG_DBG_ENTRY *dbg_log_cmd;

struct REMAP_DBG_ENTRY
{
	RSP_UINT32 t_addr_start;
	RSP_UINT32 t_addr_end;
	RSP_UINT32 remap_count;
	RSP_UINT32 t_addr[NUM_MAX_REMAP_ENTRY]; //need to recalculate addr 
	RSP_UINT32 o_addr[NUM_MAX_REMAP_ENTRY];
	RSP_UINT32 epoch;
	RSP_UINT8 remap_offset; //store remap entry number with given LPN on write and be used for confirm-read cmd
	REMAP_DBG_ENTRY* next;
	REMAP_DBG_ENTRY* before;
}; //4KB entry

REMAP_DBG_ENTRY *dbg_remap_entry;

struct TWRITE_DBG_ENTRY
{
	RSP_UINT32 addr_start;
	RSP_UINT32 io_count;
	RSP_UINT32 o_addr[NUM_MAX_TWRITE_ENTRY];
	RSP_BOOL write_complete[NUM_MAX_TWRITE_ENTRY]; //it is not necessary for the scheme but useful for debug
	RSP_UINT32 epoch;
	RSP_UINT32 remained;
	TWRITE_DBG_ENTRY* next;
	TWRITE_DBG_ENTRY* before;

}; //it is not actually 4KB

TWRITE_DBG_ENTRY *dbg_twrite_entry;

struct TWRITE_DBG_LIST
{
	RSP_UINT32 size;
	TWRITE_DBG_ENTRY *head;
};
struct REMAP_DBG_LIST
{
	RSP_UINT32 size;
	REMAP_DBG_ENTRY *head;
};

TWRITE_DBG_LIST *DBG_RW_TWRITE_LIST;
REMAP_DBG_LIST *DBG_REMAP_LIST;

//debugging 

namespace Hesper{

#define VC_MAX 0xffffffff

//Macro function
#define get_channel(lpn) ((lpn) % (NAND_NUM_CHANNELS))
#define get_bank(lpn) ((lpn) / (NAND_NUM_CHANNELS) % BANKS_PER_CHANNEL)
#define get_plane(page_offset_in_bank) (((page_offset_in_bank) / PAGES_PER_BLK) % PLANES_PER_BANK)
#define get_block(page_offset_in_bank) ((page_offset_in_bank) / PAGES_PER_BLK)
#define get_super_block(page_offset_in_bank) ((page_offset_in_bank) / (PLANES_PER_BANK *  LPAGE_PER_PPAGE * PAGES_PER_BLK))
#define get_page_offset(page_offset_in_bank) ((page_offset_in_bank) % PAGES_PER_BLK)
#define generate_vpn(channel, bank, block, plane, page, high_low) ((((channel) * BANKS_PER_CHANNEL + (bank)) * PAGES_PER_BANK + ((block) * BLKS_PER_PLANE + (plane)) * PAGES_PER_BLK + (page)) * LPAGE_PER_PPAGE + high_low)

//from vpn
#define get_channel_from_vpn(vpn) (vpn /(BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE))
#define get_vpn_offset_in_channel(vpn) (vpn % (BANKS_PER_CHANNEL * PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE))
#define get_bank_from_vpn(vpn) (get_vpn_offset_in_channel(vpn) /(PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE))
#define get_vpn_offset_in_bank(vpn) (vpn % (PLANES_PER_BANK * BLKS_PER_PLANE * PAGES_PER_BLK * LPAGE_PER_PPAGE))


#define inc_free_blk(channel, bank) NAND_bank_state[channel][bank].num_free_blk++
#define dec_free_blk(channel, bank) NAND_bank_state[channel][bank].num_free_blk--
#define is_full_bank(channel, bank) (NAND_bank_state[channel][bank].num_free_blk == 0)

#define get_cur_write_vpn(channel, bank) NAND_bank_state[channel][bank].cur_write_vpn
#define get_cur_write_vpn_RW(channel, bank) NAND_bank_state[channel][bank].cur_write_vpn_RW
#define get_cur_write_vpn_JN(channel, bank) NAND_bank_state[channel][bank].cur_write_vpn_JN

#define set_new_write_vpn(channel, bank, write_vpn) NAND_bank_state[channel][bank].cur_write_vpn = write_vpn
#define set_new_write_vpn_RW(channel, bank, write_vpn) NAND_bank_state[channel][bank].cur_write_vpn_RW = write_vpn
#define set_new_write_vpn_JN(channel, bank, write_vpn) NAND_bank_state[channel][bank].cur_write_vpn_JN = write_vpn

#define get_gc_block(channel, bank) NAND_bank_state[channel][bank].GC_BLK
#define set_gc_block(channel, bank, blk) NAND_bank_state[channel][bank].GC_BLK = blk

//write_buffer management

#define WRITE_BUFFER_BIT (0x80000000)

#define is_in_write_buffer(vpn) (RSP_BOOL)(vpn & WRITE_BUFFER_BIT)


	ATLWrapper::ATLWrapper(VFLWrapper* pVFL)
	{
		m_pVFLWrapper = pVFL;
		_COREID_ = __COREID__;

	}

	//overloading function for simulator
	ATLWrapper::ATLWrapper(VFLWrapper* pVFL, RSP_UINT32 CORE_ID){

		m_pVFLWrapper = pVFL;
		_COREID_ = CORE_ID; 
	}

	ATLWrapper::~ATLWrapper(RSP_VOID)
	{
	}

	RSP_UINT32* ATLWrapper::get_tempbuf(RSP_VOID){

		if (Temp_writebuf_idx == NUM_WRITEBUF_ENTRY)
			Temp_writebuf_idx = 0;

		return Temp_writebuf[Temp_writebuf_idx++];

	}

	RSP_BOOL
		ATLWrapper::RSP_Open(RSP_VOID)
	{
		RSP_UINT32 channel, bank, plane, block = 0, loop, profile_iter;
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		NUM_PBLK = NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * BLKS_PER_PLANE * PLANES_PER_BANK;
		NUM_LBLK = NUM_PBLK - OP_BLKS;
		NUM_CACHED_MAP = CMT_size / (BYTES_PER_SUPER_PAGE);

		//VCOUNT = (RSP_UINT32*)rspmalloc(NUM_PBLK * sizeof_u32);

		CACHE_ADDR = (RSP_UINT32*)rspmalloc(CMT_size);
		CACHE_MAPPING_TABLE = (RSP_UINT32*)rspsmalloc(NUM_CACHED_MAP * sizeof_u32);
		cache_count = (RSP_UINT32*)rspsmalloc(NUM_CACHED_MAP * sizeof_u32);
		//Map Mapping Table
		MAP_MAPPING_TABLE = (RSP_UINT32*)rspmalloc(NUM_MAP_ENTRY * sizeof_u32); //GTD
		MAP_VALID_COUNT = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK * sizeof_u32);
		MAPP2L = (RSP_UINT32*)rspmalloc(TOTAL_MAP_BLK * PAGES_PER_BLK * sizeof_u32);
		CACHE_MAP_DIRTY_TABLE = (RSP_BOOL*)rspmalloc(NUM_CACHED_MAP);

		//dbg
		//dbgbuff_cpy = (RSP_UINT32*)rspmalloc(RSP_BYTES_PER_PAGE);
		//dbg_buffflush = (RSP_UINT32 **)rspmalloc(sizeof(RSP_UINT32) * 4096);
		//dbg_buffflush_ptr = 0;
		//dbgend

		//Temp writebuffer for Normal & Joural write buffer
		//currently the buffer will be returned with round-robin manner, and Normal, Journal write should be 
		//sync write. (wait)
		Temp_writebuf = (RSP_UINT32 **)rspmalloc(sizeof(RSP_UINT32 *)* NUM_WRITEBUF_ENTRY);
		for (RSP_UINT32 iter = 0; iter < NUM_WRITEBUF_ENTRY; iter++){
			Temp_writebuf[iter] = (RSP_UINT32 *)rspmalloc(sizeof(RSP_UINT32)* RSP_BYTES_PER_PAGE);
		}
		Temp_writebuf_idx = 0;

		//SHRD entries
		twrite_hdr_entry = (TWRITE_HDR_ENTRY *)rspmalloc(NUM_MAX_TWRITE * 2 * sizeof(TWRITE_HDR_ENTRY));
		remap_hdr_entry = (REMAP_HDR_ENTRY *)rspmalloc(NUM_MAX_REMAP * sizeof(REMAP_HDR_ENTRY));
		RSPOSAL::RSP_MemSet(epoch_number, 0x00, sizeof(RSP_UINT32)* WRITE_TYPE_NUM);

		init_cmd_list(free_twrite_list);
		init_cmd_list(RW_twrite_list);
		init_cmd_list(JN_twrite_list);
		init_cmd_list(free_remap_list);
		init_cmd_list(RW_remap_list);
		init_cmd_list(JN_remap_list);
		init_cmd_list(completed_remap_list);

		for (RSP_UINT32 iter = 0; iter < NUM_MAX_TWRITE * 2; iter++){
			insert_twrite_entry(&twrite_hdr_entry[iter], &free_twrite_list);
		}
		for (RSP_UINT32 iter = 0; iter < NUM_MAX_REMAP; iter++){
			insert_remap_entry(&remap_hdr_entry[iter], &free_remap_list);
		}

		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++){
			NAND_bank_state[channel] = (NAND_bankstat *)rspmalloc(BANKS_PER_CHANNEL * sizeof(NAND_bankstat)); //need to change sizeof API into specific size
		}
		
		
		for(profile_iter = 0; profile_iter < Prof_total_num; profile_iter++)
			m_pVFLWrapper->RSP_SetProfileData(profile_iter, 0);


		//FTL initialize
		num_cached = 0;

		//bank initialize
		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
		{
			for (bank = 0; bank < BANKS_PER_CHANNEL; bank++)
			{
				NAND_bank_state[channel][bank].block_list = (block_struct *)rspmalloc(sizeof(block_struct)* BLKS_PER_BANK);
				for (loop = 0; loop < BLKS_PER_BANK; loop++){
					NAND_bank_state[channel][bank].block_list[loop].block_no = loop;
					NAND_bank_state[channel][bank].block_list[loop].remained_remap_cnt = 0;
				}
				NAND_bank_state[channel][bank].free_list.size = 0;
				NAND_bank_state[channel][bank].JN_log_list.size = 0;
				NAND_bank_state[channel][bank].JN_todo_list.size = 0;
				NAND_bank_state[channel][bank].RW_log_list.size = 0;
				NAND_bank_state[channel][bank].data_list.size = 0;
				NAND_bank_state[channel][bank].map_start = false;
				NAND_bank_state[channel][bank].meta_start = false;
				block = 0;
				for (int iter = 0; iter < WRITE_TYPE_NUM; iter++){

					NAND_bank_state[channel][bank].write_start[iter] = false;
					NAND_bank_state[channel][bank].writebuf_index[iter] = 0;
					NAND_bank_state[channel][bank].writebuf_bitmap[iter] = 0;
					for (RSP_UINT8 plane_iter = 0; plane_iter < PLANES_PER_BANK; plane_iter++){
						NAND_bank_state[channel][bank].writebuf_addr[iter][plane_iter] = NULL;
					}
				}
			
				NAND_bank_state[channel][bank].map_free_blk = MAP_ENTRY_BLK_PER_BANK - 1;
				//MAP blk
				NAND_bank_state[channel][bank].cur_map_vpn = 0;
			

				for (loop = 0; loop < MAP_ENTRY_BLK_PER_BANK; loop++)
				{
					for (plane = 0; plane < PLANES_PER_BANK; plane++)
					{
						RSP_erase_ops[plane].nChannel = (RSP_UINT8)channel;
						RSP_erase_ops[plane].nBank = (RSP_UINT8)bank;
						RSP_erase_ops[plane].nBlock = (RSP_UINT16)get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
					}
					m_pVFLWrapper->INC_ERASEPENDING();
					m_pVFLWrapper->Issue(RSP_erase_ops);
					m_pVFLWrapper->WAIT_ERASEPENDING();

					set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);
					MAP_VALID_COUNT[(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK + loop] = 0;
				}
				NAND_bank_state[channel][bank].MAP_GC_BLK = loop - 1;
				MAP_VALID_COUNT[(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK + loop - 1] = (RSP_UINT32)VC_MAX;
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				NAND_bank_state[channel][bank].cur_meta_vpn = 0;

				NAND_bank_state[channel][bank].cpybuf_addr = (RSP_UINT32*)rspmalloc(BYTES_PER_SUPER_PAGE);
				NAND_bank_state[channel][bank].GCbuf_addr = (RSP_UINT32*)rspmalloc(BYTES_PER_SUPER_PAGE);
				NAND_bank_state[channel][bank].GCbuf_index = 0;

				//Meta blk
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_erase_ops[plane].nChannel = (RSP_UINT8)channel;
					RSP_erase_ops[plane].nBank = (RSP_UINT8)bank;
					RSP_erase_ops[plane].nBlock = (RSP_UINT16)get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
				}
				m_pVFLWrapper->INC_ERASEPENDING();
				m_pVFLWrapper->Issue(RSP_erase_ops);
				m_pVFLWrapper->WAIT_ERASEPENDING();
				NAND_bank_state[channel][bank].meta_blk = block;
				set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);

				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				//GC blk
				set_gc_block(channel, bank, block);
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_erase_ops[plane].nChannel = (RSP_UINT8)channel;
					RSP_erase_ops[plane].nBank = (RSP_UINT8)bank;
					RSP_erase_ops[plane].nBlock = (RSP_UINT16)get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
				}
				m_pVFLWrapper->INC_ERASEPENDING();
				m_pVFLWrapper->Issue(RSP_erase_ops);
				m_pVFLWrapper->WAIT_ERASEPENDING();
				set_vcount(channel, bank, block++, (RSP_UINT32)VC_MAX);
				////////////////////////////////////////////////////////////////////////////////////////////////
				set_new_write_vpn(channel, bank, PAGES_PER_BLK * block);
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_erase_ops[plane].nChannel = (RSP_UINT8)channel;
					RSP_erase_ops[plane].nBank = (RSP_UINT8)bank;
					RSP_erase_ops[plane].nBlock = (RSP_UINT16)get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
				}
				m_pVFLWrapper->INC_ERASEPENDING();
				m_pVFLWrapper->Issue(RSP_erase_ops);
				m_pVFLWrapper->WAIT_ERASEPENDING();
				set_vcount(channel, bank, block, 0);
				insert_bl_tail(channel, bank, block++, &NAND_bank_state[channel][bank].data_list);

				set_new_write_vpn_RW(channel, bank, PAGES_PER_BLK * block);
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
					RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
					RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
				}
				m_pVFLWrapper->INC_ERASEPENDING();
				m_pVFLWrapper->Issue(RSP_erase_ops);
				m_pVFLWrapper->WAIT_ERASEPENDING();
				set_vcount(channel, bank, block, 0);
				insert_bl_tail(channel, bank, block++, &NAND_bank_state[channel][bank].RW_log_list);

				set_new_write_vpn_JN(channel, bank, PAGES_PER_BLK * block);
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
					RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
					RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
				}
				m_pVFLWrapper->INC_ERASEPENDING();
				m_pVFLWrapper->Issue(RSP_erase_ops);
				m_pVFLWrapper->WAIT_ERASEPENDING();
				set_vcount(channel, bank, block, 0);
				insert_bl_tail(channel, bank, block++, &NAND_bank_state[channel][bank].JN_log_list);


				NAND_bank_state[channel][bank].num_free_blk = BLKS_PER_PLANE - block;
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


				//normal blk
			
				for (; block < BLKS_PER_PLANE; block++)
				{
					insert_bl_tail(channel, bank, block, &NAND_bank_state[channel][bank].free_list);
					set_vcount(channel, bank, block, 0);
				}
			
				/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			}
		}
		for (loop = 0; loop < NUM_CACHED_MAP; loop++){
			CACHE_MAPPING_TABLE[loop] = VC_MAX;
			CACHE_MAP_DIRTY_TABLE[loop] = false;
		}
		for (loop = 0; loop < NUM_MAP_ENTRY; loop++)
			MAP_MAPPING_TABLE[loop] = (RSP_UINT32)VC_MAX;

		for (loop = 0; loop < TOTAL_MAP_BLK * PAGES_PER_BLK; loop++)
			MAPP2L[loop] = VC_MAX;
		RSPOSAL::RSP_MemSet(CACHE_ADDR, 0xffffffff, CMT_size);
		RSPOSAL::RSP_MemSet(cache_count, 0xffffffff, NUM_CACHED_MAP * sizeof_u32);



		return true;
	}

	// map read
	RSP_VOID ATLWrapper::map_read(RSP_UINT32 map_offset, RSP_UINT32 cache_offset)
	{
		RSP_UINT32 ppn, channel, bank, plane, block;
		RSPReadOp RSP_read_op;
		ppn = MAP_MAPPING_TABLE[map_offset];
		if (ppn == (RSP_UINT32)VC_MAX)
		{
			RSPOSAL::RSP_MemSet((RSP_UINT32 *)add_addr(CACHE_ADDR, BYTES_PER_SUPER_PAGE * cache_offset), 0xffffffff, BYTES_PER_SUPER_PAGE);
			return;
		}
		RSP_ASSERT(ppn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK);
		bank = map_offset / NUM_MAP_ENTRY_PER_BANK;
		channel = bank % NAND_NUM_CHANNELS;
		bank = bank / NAND_NUM_CHANNELS;

		block = ppn / PAGES_PER_BLK;
		//READ Request
		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_read_op.pData = (RSP_UINT32 *)add_addr(CACHE_ADDR, BYTES_PER_SUPER_PAGE * cache_offset + plane * RSP_BYTES_PER_PAGE);
			RSP_read_op.nReqID = RSP_INVALID_RID;
			//RSP_read_op.pSpareData = NULL;
			RSP_read_op.nChannel = (RSP_UINT8)channel;
			RSP_read_op.nBank = (RSP_UINT8) bank;
			RSP_read_op.nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + ppn % PAGES_PER_BLK);
			RSP_read_op.nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + ppn % PAGES_PER_BLK);
			RSP_read_op.bmpTargetSector = 0xff;
			RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, 0);
			RSP_read_op.m_nLPN = RSP_INVALID_LPN;
			m_pVFLWrapper->INC_READPENDING();
			m_pVFLWrapper->MetaIssue(RSP_read_op);
			m_pVFLWrapper->WAIT_READPENDING();

			//Read Complete
		}


		return;
	}

	RSP_VOID ATLWrapper::map_write(RSP_UINT32 map_offset, RSP_UINT32 cache_offset)
	{
		RSP_UINT32 channel, plane, old_vpn, victim_blk = 0, block, vcount = PAGES_PER_BLK, i, free_page, copy_cnt = 0, bank, src_lpn;
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];

		bank = map_offset / NUM_MAP_ENTRY_PER_BANK;
		channel = bank % NAND_NUM_CHANNELS;
		bank = bank / NAND_NUM_CHANNELS;

		if (NAND_bank_state[channel][bank].map_free_blk == 0 && NAND_bank_state[channel][bank].cur_map_vpn % PAGES_PER_BLK == PAGES_PER_BLK - 1)
		{
			//map erase
			for (i = 0; i < MAP_ENTRY_BLK_PER_BANK; i++)
			{
				if (vcount > get_map_vcount(channel, bank, i))
				{
					vcount = get_map_vcount(channel, bank, i);
					victim_blk = i;
				}
			}
			if (vcount != 0)
				int i = 0;
			if (channel == 0 && bank == 0)
				if (victim_blk == 0)
					int test = 0;
			free_page = NAND_bank_state[channel][bank].MAP_GC_BLK * PAGES_PER_BLK;
			for (i = 0; i < PAGES_PER_BLK; i++)
			{
				src_lpn = MAPP2L[((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK) + victim_blk * PAGES_PER_BLK + i];
				if (src_lpn == VC_MAX)
					continue;
				old_vpn = MAP_MAPPING_TABLE[src_lpn];
				block = old_vpn / PAGES_PER_BLK;

				RSP_ASSERT(old_vpn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK);
				RSP_ASSERT(src_lpn < NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK);

				if (old_vpn == (RSP_UINT32)VC_MAX)
					continue;
				if (old_vpn == victim_blk * PAGES_PER_BLK + i)
				{
					MAP_MAPPING_TABLE[src_lpn] = free_page;
					MAPP2L[((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK) + free_page++] = src_lpn;


					//READ_REQUEST

					for (plane = 0; plane < PLANES_PER_BANK; plane++)
					{
						RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].cpybuf_addr, plane * RSP_BYTES_PER_PAGE);
						RSP_read_op.nReqID = RSP_INVALID_RID;
						//RSP_read_op.pSpareData = NULL;
						RSP_read_op.nChannel = (RSP_UINT8) channel;
						RSP_read_op.nBank = (RSP_UINT8) bank;
						RSP_read_op.nBlock = (RSP_UINT16)get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + old_vpn % PAGES_PER_BLK);
						RSP_read_op.nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + old_vpn % PAGES_PER_BLK);
						RSP_read_op.bmpTargetSector = 0xff;
						RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, 0);
						RSP_read_op.m_nLPN = RSP_INVALID_LPN;
						m_pVFLWrapper->INC_READPENDING();
						m_pVFLWrapper->MetaIssue(RSP_read_op);
						m_pVFLWrapper->WAIT_READPENDING();

						//Read Complete
					}


					//WRITE_REQUEST

					block = MAP_MAPPING_TABLE[src_lpn] / PAGES_PER_BLK;
					for (plane = 0; plane < PLANES_PER_BANK; plane++)
					{
						RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].cpybuf_addr, plane * RSP_BYTES_PER_PAGE);
						RSP_write_ops[plane].pSpareData = NULL_SPARE;
						RSP_write_ops[plane].nChannel = (RSP_UINT8)channel;
						RSP_write_ops[plane].nBank = (RSP_UINT8)bank;
						RSP_write_ops[plane].nBlock = (RSP_UINT8) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + MAP_MAPPING_TABLE[src_lpn] % PAGES_PER_BLK);
						RSP_write_ops[plane].nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + MAP_MAPPING_TABLE[src_lpn] % PAGES_PER_BLK);
						RSP_write_ops[plane].m_anVPN[0] = generate_vpn(channel, bank, RSP_write_ops[plane].nBlock, plane, RSP_write_ops[plane].nPage, 0);
						RSP_write_ops[plane].m_anVPN[1] = generate_vpn(channel, bank, RSP_write_ops[plane].nBlock, plane, RSP_write_ops[plane].nPage, 1);
						RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
						RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
					}
					m_pVFLWrapper->INC_PROGRAMPENDING();
					m_pVFLWrapper->MetaIssue(RSP_write_ops);
					m_pVFLWrapper->WAIT_PROGRAMPENDING();

					//WRITE_complete

					copy_cnt++;
				}
				if (copy_cnt == vcount)
					break;
			}
			//update metadata
			set_map_vcount(channel, bank, victim_blk, (RSP_UINT32)VC_MAX);
			set_map_vcount(channel, bank, NAND_bank_state[channel][bank].MAP_GC_BLK, vcount);
			NAND_bank_state[channel][bank].MAP_GC_BLK = victim_blk;
			NAND_bank_state[channel][bank].cur_map_vpn = free_page;

			RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				RSP_erase_ops[plane].nChannel = (RSP_UINT8)channel;
				RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(victim_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);
			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_erase, 1);

			//map write		
			old_vpn = MAP_MAPPING_TABLE[map_offset];
			block = old_vpn / PAGES_PER_BLK;

			RSP_ASSERT(old_vpn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK || old_vpn == (RSP_UINT32)VC_MAX);
			if (old_vpn != (RSP_UINT32)VC_MAX)
				set_map_vcount(channel, bank, block, get_map_vcount(channel, bank, block) - 1);
			block = NAND_bank_state[channel][bank].cur_map_vpn / PAGES_PER_BLK;
			set_map_vcount(channel, bank, block, get_map_vcount(channel, bank, block) + 1);

			MAP_MAPPING_TABLE[map_offset] = NAND_bank_state[channel][bank].cur_map_vpn;
			old_vpn = NAND_bank_state[channel][bank].cur_map_vpn;
			MAPP2L[((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_vpn] = map_offset;
		}
		else
		{
			//map write
			old_vpn = MAP_MAPPING_TABLE[map_offset];
			block = old_vpn / PAGES_PER_BLK;

			RSP_ASSERT(old_vpn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK || old_vpn == (RSP_UINT32) VC_MAX);
			if (old_vpn != (RSP_UINT32)VC_MAX)
				set_map_vcount(channel, bank, block, get_map_vcount(channel, bank, block) - 1);

			if (NAND_bank_state[channel][bank].map_start)
				NAND_bank_state[channel][bank].cur_map_vpn++;
			else
				NAND_bank_state[channel][bank].map_start = true;

			block = NAND_bank_state[channel][bank].cur_map_vpn / PAGES_PER_BLK;
			set_map_vcount(channel, bank, block, get_map_vcount(channel, bank, block) + 1);

			MAP_MAPPING_TABLE[map_offset] = NAND_bank_state[channel][bank].cur_map_vpn;
			old_vpn = NAND_bank_state[channel][bank].cur_map_vpn;
			MAPP2L[((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK) + NAND_bank_state[channel][bank].cur_map_vpn] = map_offset;

			if (NAND_bank_state[channel][bank].cur_map_vpn % PAGES_PER_BLK == PAGES_PER_BLK - 1 && NAND_bank_state[channel][bank].map_free_blk != 0)
			{
				NAND_bank_state[channel][bank].map_free_blk--;
			}

		}


		//WRITE REQUEST

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(CACHE_ADDR, BYTES_PER_SUPER_PAGE * cache_offset + plane * RSP_BYTES_PER_PAGE);
			RSP_write_ops[plane].pSpareData = NULL_SPARE;
			RSP_write_ops[plane].nChannel = (RSP_UINT8) channel;
			RSP_write_ops[plane].nBank = (RSP_UINT8) bank;
			RSP_write_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + old_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane].nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + old_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_vpn(channel, bank, RSP_write_ops[plane].nBlock, plane, RSP_write_ops[plane].nPage, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_vpn(channel, bank, RSP_write_ops[plane].nBlock, plane, RSP_write_ops[plane].nPage, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;

		}

		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		m_pVFLWrapper->WAIT_PROGRAMPENDING();
		return;
	}


	//vcount management
	RSP_UINT32 ATLWrapper::get_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
	{
		RSP_UINT32 vcount = MAP_VALID_COUNT[(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK + block];
		RSP_ASSERT(vcount <= PAGES_PER_BLK || vcount == (RSP_UINT32)VC_MAX);
		return vcount;
	}
	RSP_VOID ATLWrapper::set_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount)
	{
		RSP_ASSERT(vcount <= PAGES_PER_BLK || vcount == (RSP_UINT32)VC_MAX);
		MAP_VALID_COUNT[(channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK + block] = vcount;
		return;
	}
	///////////////////////////////////////////////////////////////////////////////
	RSP_VOID ATLWrapper::RSP_BufferCopy(RSP_UINT32* pstDescBuffer, RSP_UINT32* pstSrcBuffer, RSP_SECTOR_BITMAP bmpTargetSector)
	{
	
		RSP_UINT32* pstDestBuf;
		RSP_UINT32* pstSrcBuf;

		pstDestBuf = pstDescBuffer;
		pstSrcBuf = pstSrcBuffer;

		for (RSP_UINT16 nSector = 0; nSector < SECTORS_PER_PAGE; nSector++)
		{
		
			if (RSP_CheckBit(bmpTargetSector, 1 << nSector) == TRUE)
			{
				RSPOSAL::RSP_BufferMemCpy(pstDestBuf , pstSrcBuf, BYTES_PER_SECTOR);
			}
			pstDestBuf += (BYTES_PER_SECTOR / sizeof(RSP_UINT32));
			pstSrcBuf += (BYTES_PER_SECTOR / sizeof(RSP_UINT32));
		}
		
	}
	RSP_BOOL ATLWrapper::RSP_CheckBit(RSP_SECTOR_BITMAP nVar, RSP_SECTOR_BITMAP nBit)
	{
		return (nBit == (nVar&nBit));
	}


	/*
		do_confirm_read:: 
		In order to notice the completion of remap cmd to host device driver.
		//1. make remap complete array with complete list
		//2. copy remap complete array into buff
	*/
	RSP_VOID ATLWrapper::do_confirm_read(RSP_UINT32 *buff){
		
		REMAP_HDR_ENTRY *remap_entry = completed_remap_list.head;
		CONFIRM_READ_ENTRY confirm_read_entry;

		RSPOSAL::RSP_MemSet(confirm_read_entry.complete, 0x00, NUM_MAX_REMAP);

		for (RSP_UINT32 loop = 0; loop < completed_remap_list.size; loop++, remap_entry = remap_entry->next){
			RSP_ASSERT(remap_entry);
			confirm_read_entry.complete[remap_entry->remap_offset] = 1;
			del_remap_entry(remap_entry, &completed_remap_list);
			insert_remap_entry(remap_entry, &free_remap_list);
		}
	
		RSPOSAL::RSP_MemCpy(buff, confirm_read_entry.complete, sizeof(RSP_UINT32)* NUM_MAX_REMAP);

	}

	RSP_UINT32 ATLWrapper::RSP_ReadPage(RSP_UINT32 request_ID, RSP_LPN LPN, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress)
	{
		RSP_UINT32 lpn, Prof_state;
		RSP_UINT32 bank, vpn, channel, plane, buf_offset, write_type;
		RSP_SECTOR_BITMAP read_bitmap;
		RSPReadOp RSP_read_op;
		lpn = LPN;

		/*
			Check if the read cmd is CONFIRM read 
		*/
		if (LPN >= CONFIRM_READ_CMD_IN_PAGE){
			do_confirm_read(BufferAddress);
			return true;
		}
		if(lpn >= RW_LOG_START_IN_PAGE)
			Prof_state =  Prof_RW;
		else if(lpn >= JN_LOG_START_IN_PAGE)
			Prof_state = Prof_JN;
		else
			Prof_state = Prof_SW;
		vpn = get_vpn(lpn, Prof_state);
		
		channel = get_channel_from_vpn(vpn);
		bank = get_bank_from_vpn(vpn);

		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(vpn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || vpn == VC_MAX || is_in_write_buffer(vpn));
		if (vpn != (RSP_UINT32)VC_MAX)
		{

			if (is_in_write_buffer(vpn))
			{
				vpn ^= (WRITE_BUFFER_BIT);
				//according to write_buffer layout (notice ftl.h)
				channel = vpn / (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
				vpn %= (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
				bank = vpn / (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
				vpn %= (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
				write_type = vpn / (PLANES_PER_BANK * LPAGE_PER_PPAGE);
				vpn = vpn % (PLANES_PER_BANK * LPAGE_PER_PPAGE);
				plane = vpn / LPAGE_PER_PPAGE;
				buf_offset = vpn % LPAGE_PER_PPAGE;
			
				RSP_ASSERT(plane < PLANES_PER_BANK && buf_offset < LPAGE_PER_PPAGE);

				RSP_BufferCopy(BufferAddress, (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].writebuf_addr[write_type][plane], buf_offset * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE), SectorBitmap);
				
				if(lpn >= RW_LOG_START_IN_PAGE)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_buf_read, 1);
				else if(lpn >= JN_LOG_START_IN_PAGE)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_buf_read, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_buf_read, 1);

				
				return ReadWriteBuffer;
			}
			else
			{
				if (vpn % LPAGE_PER_PPAGE)
				{
					read_bitmap = 0xff00;
				}
				else
				{
					read_bitmap = 0xff;
				}
				vpn = (vpn / LPAGE_PER_PPAGE) % PAGES_PER_BANK;
				plane = get_plane(vpn);

				//RSP_read_op.pSpareData = NULL;
				RSP_read_op.nReqID = request_ID;
				RSP_read_op.nChannel = (RSP_UINT8)channel;
				RSP_read_op.nBank = (RSP_UINT8) bank;
				RSP_read_op.nBlock = (RSP_UINT16) get_block(vpn);
				RSP_read_op.nPage = get_page_offset(vpn);
				RSP_read_op.bmpTargetSector = read_bitmap;

				if (read_bitmap == 0xff00){
					RSP_read_op.pData = (RSP_UINT32 *)sub_addr(BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, 1);
				}
				else{
					RSP_read_op.pData = (RSP_UINT32 *)BufferAddress;
					RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, 0);
				}

				RSP_read_op.m_nLPN = lpn;
				m_pVFLWrapper->INC_READPENDING();
				m_pVFLWrapper->Issue(RSP_read_op);
				m_pVFLWrapper->WAIT_READPENDING();


				if(lpn >= RW_LOG_START_IN_PAGE)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_read, 1);
				else if(lpn >= JN_LOG_START_IN_PAGE)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_read, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_read, 1);
				
				return ReadNand;

				//READ complete
			}
		}
		else
		{
			RSPOSAL::RSP_MemSet(BufferAddress, 0xFF, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);

			if(lpn >= RW_LOG_START_IN_PAGE)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Null_read, 1);
			else if(lpn >= JN_LOG_START_IN_PAGE)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Null_read, 1);
			else
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_Null_read, 1);
			
			return ReadError;

		}

		return true;
	}

	
	/*
		twrite_header_handler::
		when the twrite cmd is arrived, twrite_header_handler gets the write cmd buffer and parses the buffer into twrite entry.
		The twrite entry will be classified and inserted into Random write and Journal write according to its addr_start.
	*/
	RSP_VOID ATLWrapper::twrite_header_handler(RSP_UINT32* buff){
		//input twrite entry 
	
		TWRITE_HDR_ENTRY *twrite_entry = get_free_twrite_entry();
		del_twrite_entry(twrite_entry, &free_twrite_list);

		//RSP_BufferCopy((RSP_UINT32 *)twrite_entry, buff, SECTORS_PER_PAGE / LPAGE_PER_PPAGE);
		RSPOSAL::RSP_MemCpy((RSP_UINT32 *)twrite_entry, buff, TWRITE_HDR_BYTE_SIZE);

		RSPOSAL::RSP_MemSet((RSP_UINT32 *)twrite_entry->write_complete, 0x00, sizeof(RSP_BOOL)* NUM_MAX_TWRITE_ENTRY);
	
		if(twrite_entry->io_count % NUM_FTL_CORE == 0)
			twrite_entry->remained = twrite_entry->io_count / NUM_FTL_CORE;
		else
			twrite_entry->remained = (RSP_UINT32)(twrite_entry->addr_start % NUM_FTL_CORE == THIS_CORE) + (twrite_entry->io_count) / NUM_FTL_CORE;

		//dbg
		dbg_twrite_hdr_cnt++;

		if (twrite_entry->addr_start / NUM_FTL_CORE >= RW_LOG_START_IN_PAGE){
			//RW twrite
			twrite_entry->epoch = epoch_number[SHRD_RW];
			insert_twrite_entry(twrite_entry, &RW_twrite_list);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_twrite_cnt, 1);
		}
		else if (twrite_entry->addr_start / NUM_FTL_CORE >= JN_LOG_START_IN_PAGE){
			//JN twrite
			twrite_entry->epoch = epoch_number[SHRD_JN];
			insert_twrite_entry(twrite_entry, &JN_twrite_list);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_twrite_cnt, 1);
		}
		else{
			dbg1 = twrite_entry->addr_start;
			dbg2 = RW_LOG_START_IN_PAGE;
			dbgbuff = buff;
			dbg_twrite_entry = (TWRITE_DBG_ENTRY *)twrite_entry;

			RSP_ASSERT(0);
		}
	}

	
	/*
		find_twrite_oLPN::
		When the normal write is arrived, the write request will be classified into RW & JN log or sequential write by LPN.
		If the write is RW or JN, then we need to find oLPN (or hLPN) of corresponding write request.
		At this time, find_twrite_oLPN searches twrite_entry for corrspoding tLPN, and then, gets oLPN from the twrite_entry.
	*/
	TWRITE_HDR_ENTRY *ATLWrapper::find_twrite_oLPN(RSP_UINT32 tLPN, RSP_UINT32 *oLPN){

		TWRITE_HDR_ENTRY *twrite_entry;
		RSP_UINT32 offset;
		if (tLPN >= RW_LOG_START_IN_PAGE){
			//RW twrite
			twrite_entry = find_twrite_entry_of_tLPN(tLPN, &RW_twrite_list);
		}
		else if (tLPN >= JN_LOG_START_IN_PAGE){
			//JN twrite
			twrite_entry = find_twrite_entry_of_tLPN(tLPN, &JN_twrite_list);
		}
		else{
			RSP_ASSERT(0);
			return NULL;
		}
		offset = tLPN * NUM_FTL_CORE + THIS_CORE - twrite_entry->addr_start;
		RSP_ASSERT(twrite_entry->remained >= 0);
		
		*oLPN = twrite_entry->o_addr[offset] / NUM_FTL_CORE;

		if (twrite_entry->o_addr[offset] == RSP_INVALID_LPN)
			*oLPN = RSP_INVALID_LPN;

		//dbg
		twrite_entry->write_complete[offset] = true;

		return twrite_entry;
	}

	/*
		find_twrite_entry_of_tLPN::
		return corresponding twrite_entry for the tLPN.
	*/
	TWRITE_HDR_ENTRY *ATLWrapper::find_twrite_entry_of_tLPN(RSP_UINT32 tLPN, TWRITE_HDR_LIST *list){

		TWRITE_HDR_ENTRY *twrite_entry = list->head;
		for (RSP_UINT32 loop = 0; loop < list->size; loop++, twrite_entry = twrite_entry->next){
			RSP_ASSERT(twrite_entry);
			if (((tLPN * NUM_FTL_CORE + THIS_CORE) < (twrite_entry->addr_start + twrite_entry->io_count)) && (tLPN >= twrite_entry->addr_start / NUM_FTL_CORE)){
				return twrite_entry;
			}
		}

		dbg1 = tLPN;
		dbg2 = THIS_CORE;
		dbg3 = NUM_FTL_CORE;
		dbg_twrite_entry = (TWRITE_DBG_ENTRY *)twrite_entry;
		DBG_REMAP_LIST = (REMAP_DBG_LIST *)&RW_remap_list;
		DBG_RW_TWRITE_LIST = (TWRITE_DBG_LIST *)&RW_twrite_list;
		
		RSP_ASSERT(0);
		return NULL;
	}

	RSP_VOID ATLWrapper::insert_twrite_entry(TWRITE_HDR_ENTRY *entry, TWRITE_HDR_LIST *list){

		if (list->size == 0){

			list->head = entry;
			list->head->next = list->head;
			list->head->before = list->head;
		}
		else{
			entry->before = list->head->before;
			entry->next = list->head;
			list->head->before->next = entry;
			list->head->before = entry;
		}
		list->size++;
	}

	RSP_VOID ATLWrapper::del_twrite_entry(TWRITE_HDR_ENTRY *entry, TWRITE_HDR_LIST *list){

		if (list->head == entry)
		{
			list->head = entry->next;
		}
		entry->before->next = entry->next;
		entry->next->before = entry->before;
		list->size--;
		if (list->size == 0)
			list->head = NULL;
	}

	/*
		remap_handler::
		similar to twrite_header_handler, remap_handler hangs remap_entry into corresponding remap_list.
		After 
		'Epoch number' manages twrite and remap version, because NCQ can mix the order of twrite and remap cmd.
	*/

	RSP_VOID ATLWrapper::remap_handler(RSP_UINT32 LPN, RSP_UINT32 *buff){

		REMAP_HDR_ENTRY *remap_entry = get_free_remap_entry();
		del_remap_entry(remap_entry, &free_remap_list);

		//RSP_BufferCopy((RSP_UINT32 *)remap_entry, buff, SECTORS_PER_PAGE / LPAGE_PER_PPAGE);
		RSPOSAL::RSP_MemCpy((RSP_UINT32 *)remap_entry, buff, REMAP_HDR_BYTE_SIZE);

		remap_entry->remap_offset = (RSP_UINT8)(LPN - REMAP_CMD_IN_PAGE); //for future complete check
		/*remap_entry->epoch = epoch_number;

		epoch_number = (epoch_number + 1) % MAX_EPOCH_NUMBER;*/

		//dbg
		dbg_remap_cnt++;

		if (remap_entry->t_addr_start / NUM_FTL_CORE >= RW_LOG_START_IN_PAGE){
			remap_entry->epoch = epoch_number[SHRD_RW];
			epoch_number[SHRD_RW] = (epoch_number[SHRD_RW] + 1) % MAX_EPOCH_NUMBER;
			insert_remap_entry(remap_entry, &RW_remap_list);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_cnt, 1);
			do_remap(SHRD_RW);
		}
		else if (remap_entry->t_addr_start / NUM_FTL_CORE >= JN_LOG_START_IN_PAGE){
			remap_entry->epoch = epoch_number[SHRD_JN];
			epoch_number[SHRD_JN] = (epoch_number[SHRD_JN] + 1) % MAX_EPOCH_NUMBER;
			insert_remap_entry(remap_entry, &JN_remap_list);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_cnt, 1);
			do_remap(SHRD_JN);
		}
		else{

			dbg1 = remap_entry->t_addr_start;
			dbg2 = RW_LOG_START_IN_PAGE;
			dbgbuff = buff;
			dbg_remap_entry = (REMAP_DBG_ENTRY *)remap_entry;

			RSP_ASSERT(0);
		}
	}

	RSP_VOID ATLWrapper::do_remap(RSP_UINT8 REMAP_TYPE){
	
		RSP_UINT32 remap_size, twrite_size;
		REMAP_HDR_LIST* remap_list = (REMAP_TYPE == SHRD_RW) ? &RW_remap_list : &JN_remap_list;
		TWRITE_HDR_LIST* twrite_list = (REMAP_TYPE == SHRD_RW) ? &RW_twrite_list : &JN_twrite_list;

		if(remap_list->head == NULL)
			return;

		if (remap_list->head == NULL)
			return;

		if (remap_list->head->epoch > epoch_number[REMAP_TYPE])
			remap_size = epoch_number[REMAP_TYPE] + MAX_EPOCH_NUMBER - remap_list->head->epoch;
		else
			remap_size = epoch_number[REMAP_TYPE] - remap_list->head->epoch;
		if (twrite_list->head == NULL)
			twrite_size = 0;
		else if (twrite_list->head->epoch > epoch_number[REMAP_TYPE])
			twrite_size = epoch_number[REMAP_TYPE] + MAX_EPOCH_NUMBER - twrite_list->head->epoch;
		else
			twrite_size = epoch_number[REMAP_TYPE] - twrite_list->head->epoch;

		if (twrite_size > remap_size)
			return;
/*
		for (RSP_UINT32 channel = 0; channel < NAND_NUM_CHANNELS; channel++){
			for (RSP_UINT32 bank = 0; bank < BANKS_PER_CHANNEL; bank++){
				write_buffer_flush(channel, bank, SHRD_SW);  //for the old_vpn on the write buffer, we need to flush SW buff for reliability
				write_buffer_flush(channel, bank, REMAP_TYPE);
			}
		}*/
		for (RSP_UINT32 remap_cnt = 0; remap_cnt < remap_size - twrite_size; remap_cnt++){
			__do_remap(REMAP_TYPE);	
		}
		
	}

	RSP_VOID ATLWrapper::switch_JN_todo_log(RSP_VOID){

		for (RSP_UINT32 channel = 0; channel < NAND_NUM_CHANNELS; channel++){
			for (RSP_UINT32 bank = 0; bank < BANKS_PER_CHANNEL; bank++){
				block_struct_head *list = &NAND_bank_state[channel][bank].JN_todo_list;
				RSP_UINT32 block_no;
				while (list->size){
					block_no = list->head->block_no;
					del_blk_from_list(channel, bank, block_no, &NAND_bank_state[channel][bank].JN_todo_list);
					insert_bl_tail(channel, bank, block_no, &NAND_bank_state[channel][bank].data_list);
				}
			}
		}
		for (RSP_UINT32 channel = 0; channel < NAND_NUM_CHANNELS; channel++){
			for (RSP_UINT32 bank = 0; bank < BANKS_PER_CHANNEL; bank++){
				write_buffer_flush(channel, bank, SHRD_SW);
			}
		}
		meta_flush();
	}

	RSP_VOID ATLWrapper::__do_remap(RSP_UINT8 REMAP_TYPE){
	
		REMAP_HDR_LIST* remap_list = (REMAP_TYPE == SHRD_RW) ? &RW_remap_list : &JN_remap_list;
		REMAP_HDR_ENTRY *entry = remap_list->head;
		
		RSP_UINT32 remap_start_tLPN;
		RSP_UINT32 remap_end_tLPN;

		//dbg
		dbg_remap_entry = (REMAP_DBG_ENTRY *)entry;

		remap_start_tLPN = entry->t_addr_start / NUM_FTL_CORE;
		remap_end_tLPN = entry->t_addr_end / NUM_FTL_CORE;

		if((entry->t_addr_start % NUM_FTL_CORE) && (THIS_CORE == 0)){
			//start from odd number, this core is core 0
			remap_start_tLPN++;
		}
		if((entry->t_addr_end % NUM_FTL_CORE == 0) && THIS_CORE){
			//end with even number, this core is core 1
			remap_end_tLPN--;
		}
		
		
		for (RSP_UINT32 remained = 0; remained <= remap_end_tLPN - remap_start_tLPN; remained++){
			RSP_UINT32 vpn;
			if(REMAP_TYPE == SHRD_RW)
				vpn = get_vpn(remap_start_tLPN + remained, Prof_RW_remap);
			else
				vpn = get_vpn(remap_start_tLPN + remained, Prof_JN_remap);
			RSP_UINT32 channel = get_channel_from_vpn(vpn);
			RSP_UINT32 bank = get_bank_from_vpn(vpn);
			RSP_UINT32 bank_offset = get_vpn_offset_in_bank(vpn);
			RSP_UINT32 superblk = get_super_block(bank_offset);
			//this should be done from write_page when the write was padding.

			//20150831
			if(is_in_write_buffer(vpn))
				continue;
			
			if (vpn == VC_MAX)
				continue;
			if (channel > RSP_NUM_CHANNEL){
				dbg1 = vpn;
				dbg2 = RSP_BLOCK_PER_PLANE;
				RSP_ASSERT(0);
			}

			if(get_vcount(channel, bank, superblk) == VC_MAX){
				dbg1 = vpn;
				dbg2 = channel;
				dbg3 = bank;
				dbg4 = superblk;
				dbg_remap_entry = (REMAP_DBG_ENTRY *)entry;
				DBG_REMAP_LIST = (REMAP_DBG_LIST *)&RW_remap_list;
				DBG_RW_TWRITE_LIST = (TWRITE_DBG_LIST *)&RW_twrite_list;
				
			}
			
			set_vcount(channel, bank, superblk, get_vcount(channel, bank, superblk) - 1);
		}

		//t_addr array is sorted with o_addr.
		for (RSP_UINT32 remap_cnt = 0; remap_cnt < entry->remap_count; remap_cnt++){
			RSP_UINT32 vpn;
			RSP_UINT32 old_vpn;

			if (entry->t_addr[remap_cnt] % NUM_FTL_CORE != THIS_CORE)
				continue;
			if (entry->o_addr[remap_cnt] == VC_MAX)
				continue;
			if(REMAP_TYPE == SHRD_RW)
			{
				vpn = get_vpn(entry->t_addr[remap_cnt] / NUM_FTL_CORE, Prof_RW_remap);
				old_vpn = get_vpn(entry->o_addr[remap_cnt] / NUM_FTL_CORE, Prof_RW_remap);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_entry, 1);
			}
			else
			{
				vpn = get_vpn(entry->t_addr[remap_cnt] / NUM_FTL_CORE, Prof_JN_remap);
				old_vpn = get_vpn(entry->o_addr[remap_cnt] / NUM_FTL_CORE, Prof_JN_remap);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_entry, 1);
			}
			RSP_UINT8 channel = (RSP_UINT8) get_channel_from_vpn(vpn);
			RSP_UINT8 bank = (RSP_UINT8) get_bank_from_vpn(vpn);
			RSP_UINT32 bank_offset = get_vpn_offset_in_bank(vpn);
			RSP_UINT16 superblk = (RSP_UINT16) get_super_block(bank_offset);
			if (channel > RSP_NUM_CHANNEL){
				//twrite data is not arrived yet
				dbg1 = vpn;
				dbg2 = remap_cnt;
				dbg3 = entry->t_addr[remap_cnt];
				dbg4 = entry->o_addr[remap_cnt];
				dbg5 = old_vpn;
				dbg6 = get_vpn(entry->t_addr[remap_cnt + 2] / NUM_FTL_CORE, Prof_RW_remap);
				dbg_remap_entry = (REMAP_DBG_ENTRY *)entry;
				DBG_REMAP_LIST = (REMAP_DBG_LIST *)&RW_remap_list;
				DBG_RW_TWRITE_LIST = (TWRITE_DBG_LIST *)&RW_twrite_list;
				
				RSP_ASSERT(0);
			}
			if (old_vpn != VC_MAX){

				RSP_UINT8 old_channel, old_bank;
				RSP_UINT16 old_block;
				RSP_UINT32 old_bank_offset;
				RSP_UINT8 write_type;
				RSP_UINT32 plane;
				RSP_UINT32 buf_offset;
				if(is_in_write_buffer(old_vpn)){
					old_vpn ^= (WRITE_BUFFER_BIT);
					old_channel = old_vpn / (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
					old_vpn %= (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
					old_bank = old_vpn / (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
					old_vpn %= (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
					//when the data is remapped at buffer, we need to know the previous write_type
					write_type = old_vpn / (PLANES_PER_BANK * LPAGE_PER_PPAGE);
					old_vpn %= (PLANES_PER_BANK * LPAGE_PER_PPAGE);

					plane = old_vpn / LPAGE_PER_PPAGE;
					buf_offset = old_vpn % LPAGE_PER_PPAGE;
					NAND_bank_state[old_channel][old_bank].writebuf_lpn[write_type][plane][buf_offset][0] = RSP_INVALID_LPN;
				}
				else{
					old_channel = (RSP_UINT8) get_channel_from_vpn(old_vpn);
					old_bank = (RSP_UINT8) get_bank_from_vpn(old_vpn);
					old_bank_offset = get_vpn_offset_in_bank(old_vpn);
					old_block = (RSP_UINT16) get_super_block(old_bank_offset);
					RSP_ASSERT(get_vcount(old_channel, old_bank, old_block) && get_vcount(old_channel, old_bank, old_block) != VC_MAX);
					set_vcount(old_channel, old_bank, old_block, get_vcount(old_channel, old_bank, old_block) - 1);
				}
			}
			if(!is_in_write_buffer(vpn))
				set_vcount(channel, bank, superblk, get_vcount(channel, bank, superblk) + 1);
			if(REMAP_TYPE == SHRD_RW)
				set_vpn(entry->o_addr[remap_cnt] / NUM_FTL_CORE, vpn, Prof_RW_remap);
			else
				set_vpn(entry->o_addr[remap_cnt] / NUM_FTL_CORE, vpn, Prof_JN_remap);
				
		}

		//decrease remained count on blk struct
		for (RSP_UINT32 remained = 0; remained <= remap_end_tLPN - remap_start_tLPN; remained++){
			RSP_UINT32 vpn;
			if(REMAP_TYPE == SHRD_RW)
				vpn = get_vpn(remap_start_tLPN + remained, Prof_RW_remap);
			else
				vpn = get_vpn(remap_start_tLPN + remained, Prof_JN_remap);
		
			//this should be done from write_page when the write was padding.
			if (vpn == VC_MAX)
				continue;
			RSP_UINT32 channel = get_channel_from_vpn(vpn);
			RSP_UINT32 bank = get_bank_from_vpn(vpn);
			RSP_UINT32 bank_offset = get_vpn_offset_in_bank(vpn);
			RSP_UINT32 superblk = get_super_block(bank_offset);

			if(REMAP_TYPE == SHRD_RW)
				set_vpn(remap_start_tLPN + remained, VC_MAX, Prof_RW_remap);
			else
				set_vpn(remap_start_tLPN + remained, VC_MAX, Prof_JN_remap);
			
			if(is_in_write_buffer(vpn))
				continue;
			
			NAND_bank_state[channel][bank].block_list[superblk].remained_remap_cnt--;
			

			if (NAND_bank_state[channel][bank].block_list[superblk].remained_remap_cnt == 0){
				//should move blk group
				if (REMAP_TYPE == SHRD_RW){
					if (get_block(get_cur_write_vpn_RW(channel,bank)) == superblk)
					if (get_cur_write_vpn_RW(channel, bank) % PAGES_PER_BLK != PAGES_PER_BLK - 1)
						continue; //because the block is currently being written.
					//move block group into data block now
					del_blk_from_list(channel, bank, superblk, &NAND_bank_state[channel][bank].RW_log_list);
					insert_bl_tail(channel, bank, superblk, &NAND_bank_state[channel][bank].data_list);
				}
				else if (REMAP_TYPE == SHRD_JN){
					if (get_block(get_cur_write_vpn_JN(channel, bank)) == superblk)
					if (get_cur_write_vpn_JN(channel, bank) % PAGES_PER_BLK != PAGES_PER_BLK - 1)
						continue; //because the block is currently being written.
					//move block group into todo-list and move to data block at jsuperblock write.
					del_blk_from_list(channel, bank, superblk, &NAND_bank_state[channel][bank].JN_log_list);
					insert_bl_tail(channel, bank, superblk, &NAND_bank_state[channel][bank].JN_todo_list);
				}
				else
					RSP_ASSERT(0);
			}
		}
		del_remap_entry(entry, remap_list);

		//we don't need to confirm remap command with read because completion of the remap stands for real completion of remap command itself.
		insert_remap_entry(entry, &free_remap_list);

		//insert_remap_entry(entry, &completed_remap_list);
	}

	RSP_VOID ATLWrapper::insert_remap_entry(REMAP_HDR_ENTRY *entry, REMAP_HDR_LIST *list){

		if (list->size == 0){

			list->head = entry;
			list->head->next = list->head;
			list->head->before = list->head;
		}
		else{
			entry->before = list->head->before;
			entry->next = list->head;
			list->head->before->next = entry;
			list->head->before = entry;
		}
		list->size++;
	}

	RSP_VOID ATLWrapper::del_remap_entry(REMAP_HDR_ENTRY *entry, REMAP_HDR_LIST *list){

		if (list->head == entry)
		{
			list->head = entry->next;
		}
		entry->before->next = entry->next;
		entry->next->before = entry->before;
		list->size--;
		if (list->size == 0)
			list->head = NULL;
	}

	TWRITE_HDR_ENTRY* ATLWrapper::get_free_twrite_entry(RSP_VOID){

		RSP_ASSERT(free_twrite_list.size);
		return free_twrite_list.head;
	}

	REMAP_HDR_ENTRY* ATLWrapper::get_free_remap_entry(RSP_VOID){

		RSP_ASSERT(free_remap_list.size);
		return free_remap_list.head;
	}

	//NAND write: handling write request
	//NAND_request: cur request
	//isEPF: this req is issued by EPF
	//iscb: callback function called

	RSP_BOOL ATLWrapper::RSP_WritePage(RSP_LPN LPN[2], RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress)
	{
		RSP_UINT8 loop;
		RSP_UINT8 low_type = SHRD_SW;
		RSP_UINT8 buff_cnt = 0;
		RSP_UINT32 *temp_buf = NULL;
		RSP_BOOL ret = true;
		RSP_BOOL DO_REMAP_FLAG = false;
		TWRITE_HDR_ENTRY *twrite_entry = NULL;

		//dbg
		//RSPOSAL::RSP_MemCpy((RSP_UINT32 *)dbgbuff_cpy, BufferAddress, RSP_BYTES_PER_PAGE);
		dbglpn0 = LPN[0];
		dbglpn1 = LPN[1];
		//dbgend
		
		for (loop = 0; loop < LPAGE_PER_PPAGE; loop++)
		{
			RSP_UINT32 oLPN = RSP_INVALID_LPN;
			if (LPN[loop] < RSP_INVALID_LPN)
			{
				RSP_UINT8 write_type = SHRD_SW;
				if (LPN[loop] == JN_SUPERBLK_PAGE_IDX)
					write_type = SHRD_JN_SP;
				if (LPN[loop] >= JN_LOG_START_IN_PAGE){

					if (LPN[loop] >= CONFIRM_READ_CMD_IN_PAGE){
						//this should not be happened. confirm read area is only for read cmd
						RSP_ASSERT(0);
						continue;
					}
					else if (LPN[loop] >= REMAP_CMD_IN_PAGE){
						remap_handler(LPN[loop], (RSP_UINT32 *)add_addr(BufferAddress, loop * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
						 
						buff_cnt++;
						continue;
					}
					else if (LPN[loop] >= TWRITE_CMD_IN_PAGE){

						twrite_header_handler((RSP_UINT32 *)add_addr(BufferAddress, loop * RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
						buff_cnt++;
						
						continue;
					}
					else if (LPN[loop] >= RW_LOG_START_IN_PAGE){
						write_type = SHRD_RW;
						twrite_entry = find_twrite_oLPN(LPN[loop], &oLPN);
						twrite_entry->remained--;

						//dbg
						dbg_twrite_data_cnt++; //4KB unit (include padding)
					}
					else{
						write_type = SHRD_JN;
						twrite_entry = find_twrite_oLPN(LPN[loop], &oLPN);
						twrite_entry->remained--;
					}
				}
				if (loop){
					if (write_type == SHRD_RW){
						ret = write_page(LPN[loop], loop ? (SectorBitmap & 0xff00) : (SectorBitmap & 0xff), BufferAddress, oLPN, write_type % WRITE_TYPE_NUM);
						if(!ret){
							buff_cnt++;
						}
					}
					else if (write_type == low_type){
						write_page(LPN[loop], loop ? (SectorBitmap & 0xff00) : (SectorBitmap & 0xff), temp_buf, oLPN, write_type % WRITE_TYPE_NUM);
						buff_cnt++;

					}
					else{
						temp_buf = get_tempbuf();
						RSP_BufferCopy(temp_buf, BufferAddress, 0xffff);
						write_page(LPN[loop], loop ? (SectorBitmap & 0xff00) : (SectorBitmap & 0xff), temp_buf, oLPN, write_type % WRITE_TYPE_NUM);
						buff_cnt++;
						
					}

				}
				else{
					low_type = write_type;
					if (write_type == SHRD_RW){
						ret = write_page(LPN[loop], loop ? (SectorBitmap & 0xff00) : (SectorBitmap & 0xff), BufferAddress, oLPN, write_type % WRITE_TYPE_NUM);
						if(!ret)
							buff_cnt++;
						
					}
					else{
						temp_buf = get_tempbuf();
						RSP_BufferCopy(temp_buf, BufferAddress, 0xffff);
						write_page(LPN[loop], loop ? (SectorBitmap & 0xff00) : (SectorBitmap & 0xff), temp_buf, oLPN, write_type % WRITE_TYPE_NUM);
						buff_cnt++;
						
					}
				}
				
			
				if (write_type == SHRD_JN_SP)
					switch_JN_todo_log();
				else if (write_type != SHRD_SW){
					if (twrite_entry->remained == 0){
						//do_remap(write_type);
						DO_REMAP_FLAG = true;
						del_twrite_entry(twrite_entry, (write_type == SHRD_RW) ? &RW_twrite_list : &JN_twrite_list);
						insert_twrite_entry(twrite_entry, &free_twrite_list);
					}
				}
			}
			else{
				buff_cnt++;
			}
		}

		//need to modify if apply JN on the SHRD
		if (DO_REMAP_FLAG){
			do_remap(SHRD_RW);
		}
		
		if(buff_cnt == LPAGE_PER_PPAGE)
			return false;
		return true;
	}
	//write_page: write one page
	//lpn: logical page number
	//sect offset: start sector
	//cnt: sect count to write
	//t_num: transaction number (it used in complete state)
	RSP_BOOL ATLWrapper::write_page(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress, RSP_UINT32 oLPN, RSP_UINT8 WRITE_TYPE)
	{
		RSP_UINT32 channel, bank, old_channel, old_bank, old_vpn, new_vpn, block;
		RSP_UINT32 plane, buf_offset, cnt = 0, plane_ppn[LPAGE_PER_PPAGE], super_blk, write_type;
		RSP_UINT32 vpage_offset = 0;
		RSP_UINT8 bitmap;
		RSP_BOOL find_first = false;
		RSP_BOOL ret = true;  //when the buffer is not used, then switch into false
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSP_SECTOR_BITMAP RMW_bitmap = 0;
		channel = get_channel(lpn);
		bank = get_bank(lpn);

		//Padding Write
		//need to set vpn to VC_MAX (because the tLPN has no more corresponding VPN)
		if (WRITE_TYPE == SHRD_RW && oLPN >= RSP_INVALID_LPN){
			set_vpn(lpn, VC_MAX, Prof_RW);
			return false;
		}

		if(WRITE_TYPE != SHRD_SW)
			old_vpn = VC_MAX;
		else
			old_vpn = get_vpn(lpn, Prof_SW);
		

		RSP_ASSERT(lpn < TWRITE_CMD_IN_PAGE);

		if(WRITE_TYPE == SHRD_SW){
			RSP_ASSERT(lpn < JN_LOG_START_IN_PAGE);
		}
		else if(WRITE_TYPE == SHRD_JN){
			RSP_ASSERT(lpn < RW_LOG_START_IN_PAGE && lpn >= JN_LOG_START_IN_PAGE);
		}
		else if(WRITE_TYPE == SHRD_RW){
			RSP_ASSERT(lpn < TWRITE_CMD_IN_PAGE && lpn >= RW_LOG_START_IN_PAGE);
		}
		
		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(old_vpn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || old_vpn == VC_MAX || is_in_write_buffer(old_vpn));

		if (is_in_write_buffer(old_vpn) && old_vpn != VC_MAX)
		{
		
			old_vpn ^= (WRITE_BUFFER_BIT);
			channel = old_vpn / (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
			old_vpn %= (BANKS_PER_CHANNEL * WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
			bank = old_vpn / (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
			old_vpn %= (WRITE_TYPE_NUM * PLANES_PER_BANK * LPAGE_PER_PPAGE);
			//when the data is remapped at buffer, we need to know the previous write_type
			write_type = old_vpn / (PLANES_PER_BANK * LPAGE_PER_PPAGE);
			old_vpn %= (PLANES_PER_BANK * LPAGE_PER_PPAGE);

			plane = old_vpn / LPAGE_PER_PPAGE;
			buf_offset = old_vpn % LPAGE_PER_PPAGE;

			if (SectorBitmap >> RSP_SECTOR_PER_LPN){
				NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][plane] |= (SectorBitmap >> RSP_SECTOR_PER_LPN) << (buf_offset * RSP_SECTOR_PER_LPN);
				RSP_BufferCopy((RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].writebuf_addr[write_type][plane], buf_offset * RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN),
					(RSP_UINT32 *)add_addr(BufferAddress, RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN), SectorBitmap >> RSP_SECTOR_PER_LPN);
				
			}
			else{
				NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][plane] |= SectorBitmap << (buf_offset * RSP_SECTOR_PER_LPN);
				RSP_BufferCopy((RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].writebuf_addr[write_type][plane], buf_offset * RSP_BYTE_PER_SECTOR * RSP_SECTOR_PER_LPN),
					BufferAddress, SectorBitmap);
			}
			if(WRITE_TYPE == SHRD_SW)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_modify_buf_write, 1);
			else if(WRITE_TYPE == SHRD_RW)
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_modify_buf_write, 1);
			else
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_modify_buf_write, 1);
			return false;
		}
		else
		{
			old_channel = get_channel_from_vpn(old_vpn);
			old_bank = get_bank_from_vpn(old_vpn);

			plane = get_plane((old_vpn / LPAGE_PER_PPAGE) % PAGES_PER_BANK);
			block = get_super_block(old_vpn % (LPAGE_PER_PPAGE * PAGES_PER_BANK));

			if (old_vpn != VC_MAX)
			{
				if (old_vpn % LPAGE_PER_PPAGE)
					vpage_offset = 1;
				old_vpn = (old_vpn / LPAGE_PER_PPAGE) % PAGES_PER_BANK; //8KB page in bank
				if (!(SectorBitmap == 0xff) && !(SectorBitmap == 0xff00))
				{ //Read Modify
				
					if (vpage_offset)
						RSP_read_op.pData = (RSP_UINT32 *)sub_addr(BufferAddress, RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					else
						RSP_read_op.pData = (RSP_UINT32 *)BufferAddress;
					//RSP_read_op.pSpareData = NULL;
					RSP_read_op.nReqID = RSP_INVALID_RID;
					RSP_read_op.nChannel = (RSP_UINT8) old_channel;
					RSP_read_op.nBank = (RSP_UINT8) old_bank;
					RSP_read_op.nBlock = (RSP_UINT16) get_block(old_vpn);
					RSP_read_op.nPage = get_page_offset(old_vpn);
					if (SectorBitmap >> RSP_SECTOR_PER_LPN){
						if (vpage_offset)
						RSP_read_op.bmpTargetSector = (~SectorBitmap) & 0xff00;
						else
						RSP_read_op.bmpTargetSector = ((~SectorBitmap) >> RSP_SECTOR_PER_LPN) & 0xff;
						
						SectorBitmap = 0xff00;
					}
					else{
						if (vpage_offset)
						RSP_read_op.bmpTargetSector = ((~SectorBitmap) << RSP_SECTOR_PER_LPN) & 0xff00;
						else
						RSP_read_op.bmpTargetSector = (~SectorBitmap) & 0xff;
						SectorBitmap = 0xff;
					}
					RSP_read_op.m_nVPN = generate_vpn(old_channel, old_bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, vpage_offset);
					RSP_read_op.m_nLPN = lpn;

					m_pVFLWrapper->INC_READPENDING();
					m_pVFLWrapper->Issue(RSP_read_op);

					//Read Complete
					m_pVFLWrapper->WAIT_READPENDING();

					if(WRITE_TYPE == SHRD_SW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_modify_read, 1);
					else if(WRITE_TYPE == SHRD_RW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_modify_read, 1);
					else
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_modify_read, 1);
				}

				set_vcount(old_channel, old_bank, block, get_vcount(old_channel, old_bank, block) - 1);
			}
			else
			{
				block = 0;
			}
			RSP_ASSERT(block < BLKS_PER_PLANE);

			//buffer bitmap:: 00 00 00 00
			//write_buf_idx:: 3  2  1  0
			//write_buf_off:: 10 10 10 10
			RSP_UINT32 idx = NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE];
			if (idx == 0){
				//first buff access after write
				//new buff ptr
				NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][idx] = BufferAddress;

				if (SectorBitmap >> SECTORS_PER_LPN){
					NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << (idx * LPAGE_PER_PPAGE + 1); //buffoffset 1
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][1][REQ_LPN] = lpn;
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][1][REMAP_LPN] = oLPN;
					set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + (idx * LPAGE_PER_PPAGE + 1)), WRITE_TYPE);
				}
				else{
					NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << (idx * LPAGE_PER_PPAGE); //buffoffset 0
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][0][REQ_LPN] = lpn;
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][0][REMAP_LPN] = oLPN;
					set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + (idx * LPAGE_PER_PPAGE)), WRITE_TYPE);
				}
				NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][idx] = SectorBitmap;
				NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE]++;
			}
			else if ((SectorBitmap >> SECTORS_PER_LPN) && (BufferAddress == NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][idx - 1])){
				//buff tail	
				NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx - 1][1][REQ_LPN] = lpn;
				NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx - 1][1][REMAP_LPN] = oLPN;
				NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << ((idx - 1) * LPAGE_PER_PPAGE + 1);
				set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + ((idx - 1) * LPAGE_PER_PPAGE + 1)), WRITE_TYPE);
				NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][idx - 1] |= SectorBitmap;
			}
			else if (idx < PLANES_PER_BANK){
				//new buff ptr
				NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][idx] = BufferAddress;

				if (SectorBitmap >> SECTORS_PER_LPN){
					NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << (idx * LPAGE_PER_PPAGE + 1); //buffoffset 1
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][1][REQ_LPN] = lpn;
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][1][REMAP_LPN] = oLPN;
					set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + (idx * LPAGE_PER_PPAGE + 1)), WRITE_TYPE);
				}
				else{
					NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << (idx * LPAGE_PER_PPAGE); //buffoffset 0
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][0][REQ_LPN] = lpn;
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][idx][0][REMAP_LPN] = oLPN;
					set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + (idx * LPAGE_PER_PPAGE)), WRITE_TYPE);
				}
				NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE]++;
				NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][idx] = SectorBitmap;

			}
			else if (NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] ^ 0){
				//has partial (4KB) buff, fill it!
				bitmap = 0x1;
				for (RSP_UINT8 buff_iter = 0; buff_iter < PLANES_PER_BANK * LPAGE_PER_PPAGE; buff_iter++){
					if (!(NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] & (bitmap << buff_iter))){
						if (buff_iter % LPAGE_PER_PPAGE){
							if (SectorBitmap >> RSP_SECTOR_PER_LPN){
								RSP_BufferCopy(NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE], BufferAddress, SectorBitmap);
								NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE] |= SectorBitmap;
							}
							else{
								RSP_BufferCopy((RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE], RSP_SECTOR_PER_LPN * RSP_BYTE_PER_SECTOR), BufferAddress, SectorBitmap);
								NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE] |= (SectorBitmap << RSP_SECTOR_PER_LPN) & 0xff00;
							}
						}
						else{
							if (SectorBitmap >> RSP_SECTOR_PER_LPN){
								RSP_BufferCopy(NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE], (RSP_UINT32 *)add_addr(BufferAddress, RSP_SECTOR_PER_LPN * RSP_BYTE_PER_SECTOR), SectorBitmap >> RSP_SECTOR_PER_LPN);
								NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE] |= (SectorBitmap >> RSP_SECTOR_PER_LPN) & 0xff;
							}
							else{
								RSP_BufferCopy(NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE], BufferAddress, SectorBitmap);
								NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][buff_iter / LPAGE_PER_PPAGE] |= SectorBitmap;
							}
						}
						ret = false;
						NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] |= 1 << buff_iter;
						NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][buff_iter / 2][buff_iter % 2][REQ_LPN] = lpn;
						NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][buff_iter / 2][buff_iter % 2][REMAP_LPN] = oLPN;
						set_vpn(lpn, WRITE_BUFFER_BIT ^ (((channel * BANKS_PER_CHANNEL + bank) * WRITE_TYPE_NUM + WRITE_TYPE) * PLANES_PER_BANK * LPAGE_PER_PPAGE + (buff_iter)), WRITE_TYPE);
						break;
					}
				}
			}
			else
				RSP_ASSERT(0);

			if (NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] == 0xFF) //buff full
			{
				RSP_UINT32 vcount = 0;
				if (WRITE_TYPE == SHRD_SW)
					new_vpn = assign_new_write_vpn(channel, bank); //super page size
				else if (WRITE_TYPE == SHRD_RW)
					new_vpn = assign_new_write_vpn_RW(channel, bank);
				else
					new_vpn = assign_new_write_vpn_JN(channel, bank);

				RSP_ASSERT(new_vpn < BLKS_PER_BANK * PAGES_PER_BLK);
				super_blk = new_vpn / PAGES_PER_BLK;
				for (plane = 0; plane < PLANES_PER_BANK; plane++)
				{
					for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
					{
						plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;

						//20150831
						if(NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][buf_offset][REQ_LPN] != RSP_INVALID_LPN){
							set_vpn(NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][buf_offset][REQ_LPN], plane_ppn[buf_offset], WRITE_TYPE);
							vcount++;
						}
						else{
							RSP_ASSERT(NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][buf_offset][REQ_LPN] < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
							RSP_ASSERT(plane_ppn[buf_offset] < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || plane_ppn[buf_offset] == VC_MAX || is_in_write_buffer(plane_ppn[buf_offset]));
						}
					}

					RSP_write_ops[plane].pData = NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][plane];
					RSP_write_ops[plane].pSpareData = NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][0];
					RSP_write_ops[plane].bmpTargetSector = NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][plane];
					RSP_write_ops[plane].nChannel = (RSP_UINT8) channel;
					RSP_write_ops[plane].nBank = (RSP_UINT8) bank;
					RSP_write_ops[plane].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);

					RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
					RSP_write_ops[plane].m_anVPN[0] = plane_ppn[0];
					RSP_write_ops[plane].m_anVPN[1] = plane_ppn[1];
					RSP_write_ops[plane].m_anLPN[0] = NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][0][REQ_LPN];
					RSP_write_ops[plane].m_anLPN[1] = NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane][1][REQ_LPN];

				}

				m_pVFLWrapper->INC_PROGRAMPENDING();
				if (WRITE_TYPE == SHRD_RW)
					m_pVFLWrapper->Issue(RSP_write_ops);
				else{
					m_pVFLWrapper->MetaIssue(RSP_write_ops);
					//m_pVFLWrapper->WAIT_PROGRAMPENDING();
				}

				//m_pVFLWrapper->WAIT_PROGRAMPENDING();

				block = new_vpn / PAGES_PER_BLK;
				NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE] = 0;
				NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] = 0;
				for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++){
					NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][iter] = NULL;
					NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][iter] = 0;
				}
				//set_vcount(channel, bank, block, get_vcount(channel, bank, block) + PLANES_PER_BANK * LPAGE_PER_PPAGE);
				set_vcount(channel, bank, block, get_vcount(channel, bank, block) + vcount);
				if(WRITE_TYPE == SHRD_SW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				else if(WRITE_TYPE == SHRD_RW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
				if (WRITE_TYPE != SHRD_SW){
					NAND_bank_state[channel][bank].block_list[block].remained_remap_cnt += PLANES_PER_BANK * LPAGE_PER_PPAGE;
				}
			}

		}
		return ret;
	}
	//assign_new_write_vpn: it returns new page for data block
	RSP_UINT32 ATLWrapper::assign_new_write_vpn(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 write_vpn;
		RSP_UINT32 block;
		RSP_UINT32 plane;
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		write_vpn = get_cur_write_vpn(channel, bank);
		block = write_vpn / PAGES_PER_BLK;

		if ((write_vpn % PAGES_PER_BLK) == (PAGES_PER_BLK - 1))
		{
			//GC occured
			block = get_free_block(channel, bank);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_block_alloc, 1);
			if (!block)
			{
				garbage_collection(channel, bank);
				flush_bank(channel, bank);
				return get_cur_write_vpn(channel, bank);
			}
			del_blk_from_list(channel, bank, block, &NAND_bank_state[channel][bank].free_list);
			insert_bl_tail(channel, bank, block, &NAND_bank_state[channel][bank].data_list);
			flush_bank(channel, bank);
		}
		if (block != (write_vpn / PAGES_PER_BLK))
		{
			//new block
			write_vpn = block * PAGES_PER_BLK;
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
				RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);
			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Init_erase, 1);
		}
		else if (NAND_bank_state[channel][bank].write_start[SHRD_SW])
			write_vpn++;
		else
			NAND_bank_state[channel][bank].write_start[SHRD_SW] = true;
		//bank satus update
		set_new_write_vpn(channel, bank, write_vpn);
		return write_vpn;
	}

	//assign_new_write_vpn for twrite (JN and RW)

	RSP_UINT32 ATLWrapper::assign_new_write_vpn_RW(RSP_UINT32 channel, RSP_UINT32 bank){
	
		RSP_UINT32 write_vpn;
		RSP_UINT32 block;
		RSP_UINT32 plane;
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		write_vpn = get_cur_write_vpn_RW(channel, bank);
		block = write_vpn / PAGES_PER_BLK;

		if ((write_vpn % PAGES_PER_BLK) == (PAGES_PER_BLK - 1))
		{
			//GC occured
			block = get_free_block(channel, bank);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_block_alloc, 1);
			if (!block)
			{
				block = inter_GC(channel, bank);
				RSP_ASSERT(block < BLKS_PER_PLANE);
				flush_bank(channel, bank);
			}
			del_blk_from_list(channel, bank, block, &NAND_bank_state[channel][bank].free_list);
			insert_bl_tail(channel, bank, block, &NAND_bank_state[channel][bank].RW_log_list);
			flush_bank(channel, bank);
		}
		if (block != (write_vpn / PAGES_PER_BLK))
		{
			//new block
			write_vpn = block * PAGES_PER_BLK;
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
				RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);
			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Init_erase, 1);
		}
		else if (NAND_bank_state[channel][bank].write_start[SHRD_RW])
			write_vpn++;
		else
			NAND_bank_state[channel][bank].write_start[SHRD_RW] = true;
		//bank satus update
		set_new_write_vpn_RW(channel, bank, write_vpn);
		return write_vpn;
	
	}

	RSP_UINT32 ATLWrapper::assign_new_write_vpn_JN(RSP_UINT32 channel, RSP_UINT32 bank){
	
		RSP_UINT32 write_vpn;
		RSP_UINT32 block;
		RSP_UINT32 plane;
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		write_vpn = get_cur_write_vpn_JN(channel, bank);
		block = write_vpn / PAGES_PER_BLK;

		if ((write_vpn % PAGES_PER_BLK) == (PAGES_PER_BLK - 1))
		{
			//GC occured
			block = get_free_block(channel, bank);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_block_alloc, 1);
			if (!block)
			{
				block = inter_GC(channel, bank);
				RSP_ASSERT(block < BLKS_PER_PLANE);
				flush_bank(channel, bank);
			}
			del_blk_from_list(channel, bank, block, &NAND_bank_state[channel][bank].free_list);
			insert_bl_tail(channel, bank, block, &NAND_bank_state[channel][bank].JN_log_list);
			flush_bank(channel, bank);
		}
		if (block != (write_vpn / PAGES_PER_BLK))
		{
			//new block
			write_vpn = block * PAGES_PER_BLK;
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
				RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);
			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_Init_erase, 1);
		}
		else if (NAND_bank_state[channel][bank].write_start[SHRD_JN])
			write_vpn++;
		else
			NAND_bank_state[channel][bank].write_start[SHRD_JN] = true;
		//bank satus update
		set_new_write_vpn_JN(channel, bank, write_vpn);
		return write_vpn;
	}

	//L2P management
	///////////////////////////////////////////////////////////////////////////////
	//get_vpn: return L2P table value
	//lpn: logical page number
	//map_latency latency is added this value
	RSP_UINT32 ATLWrapper::get_vpn(RSP_UINT32 lpn, RSP_UINT32 flag)
	{
		RSP_UINT32 map_page, loop, loop2, return_val, cache_slot, map_page_offset;
		map_page = lpn / NUM_PAGES_PER_MAP;
		map_page_offset = lpn % NUM_PAGES_PER_MAP;

		//dbg
		if (lpn == 13652)
			RSP_UINT32 err = 3;
		//dbg

		//dbg
		if (lpn == 28786)
			RSP_UINT32 err = 3;
		//dbg

		if(lpn >= NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE){
			dbg1 = lpn;
			dbg2 = NUM_LBLK;
			RSP_ASSERT(0);
		}

		//RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(map_page < NUM_MAP_ENTRY * LPAGE_PER_PPAGE);
		//check this map_page is in SRAM
		for (loop = 0; loop < NUM_CACHED_MAP; loop++)
		{
			if (CACHE_MAPPING_TABLE[loop] == map_page)
				break;
		}

		if (loop == NUM_CACHED_MAP)
		{
			//cache miss
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_miss, 1);
			if (num_cached != NUM_CACHED_MAP)
			{
				//cache have empty slot
				if(flag == Prof_SW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_load, 1);
				else if(flag == Prof_RW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_load, 1);
				else if(flag == Prof_JN)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_load, 1);
				else if(flag == Prof_JN_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_load, 1);
				else if(flag == Prof_RW_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_load, 1);
				else if(flag == Prof_IntraGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_load, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_load, 1);
				
				map_read(map_page, num_cached);

				for (loop2 = 0; loop2 < num_cached; loop2++)
				{
					cache_count[loop2]++;
				}
				//meta update
				CACHE_MAPPING_TABLE[num_cached] = map_page;
				cache_count[num_cached] = 1;
				cache_slot = num_cached;
				num_cached++;
			}
			else
			{
				//cache is full
				//selete LRU victim
				for (loop2 = 0; loop2 < NUM_CACHED_MAP; loop2++)
				{
					if (cache_count[loop2] == NUM_CACHED_MAP)
						break;
				}

				if (CACHE_MAPPING_TABLE[loop2] != (RSP_UINT32)VC_MAX)
				{
					//write victim 
					if(flag == Prof_SW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_log, 1);
					else if(flag == Prof_RW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_log, 1);
					else if(flag == Prof_JN)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_log, 1);
					else if(flag == Prof_JN_remap)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_log, 1);
					else if(flag == Prof_RW_remap)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_log, 1);
					else if(flag == Prof_IntraGC)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_log, 1);
					else
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_log, 1);
					map_write(CACHE_MAPPING_TABLE[loop2], loop2); //sync
					CACHE_MAPPING_TABLE[loop2] = (RSP_UINT32)VC_MAX;
				}

				map_read(map_page, loop2);
				if(flag == Prof_SW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_load, 1);
				else if(flag == Prof_RW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_load, 1);
				else if(flag == Prof_JN)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_load, 1);
				else if(flag == Prof_JN_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_load, 1);
				else if(flag == Prof_RW_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_load, 1);
				else if(flag == Prof_IntraGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_load, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_load, 1);
				
				CACHE_MAPPING_TABLE[loop2] = map_page;
				cache_slot = loop2;
				for (loop = 0; loop < NUM_CACHED_MAP; loop++)
					cache_count[loop]++;
				cache_count[loop2] = 1;

			}
		}
		else
		{
			//cache hit
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_hit, 1);
			//update LRU value
			for (loop2 = 0; loop2 < num_cached; loop2++)
			{
				if (cache_count[loop2] < cache_count[loop])
					cache_count[loop2]++;
			}
			cache_count[loop] = 1;
			cache_slot = loop;
			//latency is 0
		}

		RSPOSAL::RSP_MemCpy(&return_val, (RSP_UINT32 *)add_addr(CACHE_ADDR, (cache_slot * BYTES_PER_SUPER_PAGE) + map_page_offset * sizeof_u32), sizeof_u32);

		return return_val;
	}
	//set_vpn: set L2P table value
	//lpn: logical page number
	//map_latency latency is added this value

	RSP_VOID ATLWrapper::set_vpn(RSP_UINT32 lpn, RSP_UINT32 vpn, RSP_UINT32 flag)
	{
		RSP_UINT32 map_page, loop, loop2, cache_slot, map_page_offset;
		map_page = lpn / NUM_PAGES_PER_MAP;
		map_page_offset = lpn % NUM_PAGES_PER_MAP;

		RSP_ASSERT(lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
		RSP_ASSERT(map_page < NUM_MAP_ENTRY * LPAGE_PER_PPAGE);

		//dbg
		if (lpn == 28786)
			RSP_UINT32 err = 3;
		//dbg

		//check this map_page is in DRAM
		for (loop = 0; loop < NUM_CACHED_MAP; loop++)
		{
			if (CACHE_MAPPING_TABLE[loop] == map_page)
				break;
		}

		if (loop == NUM_CACHED_MAP)
		{
			//cache miss
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_miss, 1);
			if (num_cached != NUM_CACHED_MAP)
			{
				//cache have empty slot

				map_read(map_page, num_cached);
				
				if(flag == Prof_SW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_load, 1);
				else if(flag == Prof_RW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_load, 1);
				else if(flag == Prof_JN)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_load, 1);
				else if(flag == Prof_JN_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_load, 1);
				else if(flag == Prof_RW_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_load, 1);
				else if(flag == Prof_IntraGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_load, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_load, 1);
				
				for (loop2 = 0; loop2 < num_cached; loop2++)
				{
					cache_count[loop2]++;
				}
				//meta update
				CACHE_MAPPING_TABLE[num_cached] = map_page;
				cache_count[num_cached] = 1;
				cache_slot = num_cached;
				num_cached++;
			}
			else
			{
				//cache is full
				//selete LRU victim
				for (loop2 = 0; loop2 < NUM_CACHED_MAP; loop2++)
				{
					if (cache_count[loop2] == NUM_CACHED_MAP)
						break;
				}

				if (CACHE_MAPPING_TABLE[loop2] != (RSP_UINT32)VC_MAX)
				{
					//write victim 
					if(flag == Prof_SW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_log, 1);
					else if(flag == Prof_RW)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_log, 1);
					else if(flag == Prof_JN)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_log, 1);
					else if(flag == Prof_JN_remap)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_log, 1);
					else if(flag == Prof_RW_remap)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_log, 1);
					else if(flag == Prof_IntraGC)
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_log, 1);
					else
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_log, 1);
				
					map_write(CACHE_MAPPING_TABLE[loop2], loop2); //sync
					CACHE_MAPPING_TABLE[loop2] = (RSP_UINT32)VC_MAX;
				}

				map_read(map_page, loop2);
				
				if(flag == Prof_SW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_map_load, 1);
				else if(flag == Prof_RW)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_map_load, 1);
				else if(flag == Prof_JN)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_map_load, 1);
				else if(flag == Prof_JN_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_Remap_map_load, 1);
				else if(flag == Prof_RW_remap)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_Remap_map_load, 1);
				else if(flag == Prof_IntraGC)
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_map_load, 1);
				else
					m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_map_load, 1);
				
				CACHE_MAPPING_TABLE[loop2] = map_page;
				cache_slot = loop2;
				for (loop = 0; loop < NUM_CACHED_MAP; loop++)
					cache_count[loop]++;
				cache_count[loop2] = 1;

			}
		}
		else
		{
			//cache hit
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_hit, 1);
			//update LRU value
			for (loop2 = 0; loop2 < num_cached; loop2++)
			{
				if (cache_count[loop2] < cache_count[loop])
					cache_count[loop2]++;
			}
			cache_count[loop] = 1;
			cache_slot = loop;
			//latency is 0
		}

		RSPOSAL::RSP_MemCpy((RSP_UINT32 *)add_addr(CACHE_ADDR, (cache_slot * BYTES_PER_SUPER_PAGE) + map_page_offset * sizeof_u32), &vpn, sizeof_u32);
		CACHE_MAP_DIRTY_TABLE[cache_slot] = true;

		return;
	}
	//vcount management
	////////////////////////////////////////////////////////////////////////////
	//get_vcount: return vcount of block
	//set_vcount  set vcount of input block
	RSP_UINT32 ATLWrapper::get_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
	{
		RSP_UINT32 vcount = NAND_bank_state[channel][bank].block_list[block].vcount;
		RSP_ASSERT(vcount <= PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE || vcount == (RSP_UINT32)VC_MAX);
		return vcount;
	}
	RSP_VOID ATLWrapper::set_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount)
	{
		RSP_ASSERT(vcount <= PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE || vcount == (RSP_UINT32)VC_MAX);
		NAND_bank_state[channel][bank].block_list[block].vcount = vcount;
		return;
	}
	//GC
	/////////////////////////////////////////////////////////////////////////////
	RSP_VOID ATLWrapper::garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 plane, high_low, vt_block, vcount, gc_block, free_vpn, src_page, spare_area[LPAGE_PER_PPAGE * NUM_SPARE_LPN], old_vpn, cpy_cnt = 0, buf_offset, new_vpn;
		RSP_UINT32 plane_ppn[LPAGE_PER_PPAGE], plane_lpn[LPAGE_PER_PPAGE], block, to_write, super_blk, read_vpn, write_plane, spare_lpn;
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		vt_block = get_vt_vblock(channel, bank);
		vcount = get_vcount(channel, bank, vt_block);
		gc_block = get_gc_block(channel, bank);

		set_vcount(channel, bank, gc_block, 0);

		RSP_ASSERT(vt_block < BLKS_PER_PLANE);
		RSP_ASSERT(gc_block < BLKS_PER_PLANE);
		
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_num, 1);
		
		free_vpn = gc_block * PAGES_PER_BLK;

		for (src_page = 0; src_page < PAGES_PER_BLK; src_page++)
		{
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				for (high_low = 0; high_low < LPAGE_PER_PPAGE; high_low++)
				{
					old_vpn = vt_block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + src_page % PAGES_PER_BLK;
					/*if (high_low)
						RSP_read_op.pData = (RSP_UINT32 *)sub_addr(add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE)), RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					else
						RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));*/
					if (!high_low){
						RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
						RSP_read_op.nReqID = RSP_INVALID_RID;
						RSP_read_op.nChannel = (RSP_UINT8) channel;
						//RSP_read_op.pSpareData = spare_area;
						RSP_read_op.nBank = (RSP_UINT8) bank;
						RSP_read_op.nBlock = (RSP_UINT16) get_block(old_vpn);
						RSP_read_op.nPage = get_page_offset(old_vpn);
						RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, high_low);

						m_pVFLWrapper->INC_READPENDING();

						m_pVFLWrapper->MetaIssue(RSP_read_op);

						m_pVFLWrapper->WAIT_READPENDING();

						m_pVFLWrapper->_GetSpareData(spare_area);
						m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_read, 1);
					}

					//spare_lpn = spare_area[high_low * LPAGE_PER_PPAGE];
					//if REMAP_LPN is not RSP_INVALID_LPN, we need to copy the page with REMAP_LPN rather than REQ_LPN.
					if (spare_area[high_low * LPAGE_PER_PPAGE + 1] != RSP_INVALID_LPN)
						spare_lpn = spare_area[high_low * LPAGE_PER_PPAGE + 1];
					else 
						spare_lpn = spare_area[high_low * LPAGE_PER_PPAGE];

					if (spare_lpn == RSP_INVALID_LPN)
						continue;

					read_vpn = get_vpn(spare_lpn, Prof_IntraGC);
					old_vpn += ((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK);

					RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
					RSP_ASSERT(read_vpn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_vpn == VC_MAX || is_in_write_buffer(read_vpn));
					//Read complete
					if (old_vpn * LPAGE_PER_PPAGE + high_low == read_vpn)
					{
						//copy
						write_plane = NAND_bank_state[channel][bank].GCbuf_index / 2;
						buf_offset = NAND_bank_state[channel][bank].GCbuf_index % 2;

						NAND_bank_state[channel][bank].GCbuf_index++;
						NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][REQ_LPN] = spare_lpn;

						if (NAND_bank_state[channel][bank].GCbuf_index == PLANES_PER_BANK * LPAGE_PER_PPAGE)
						{
							new_vpn = free_vpn++;
							super_blk = new_vpn / PAGES_PER_BLK;
							for (write_plane = 0; write_plane < PLANES_PER_BANK; write_plane++)
							{
								for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
								{
									plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + write_plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;
									set_vpn(NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][buf_offset][REQ_LPN], plane_ppn[buf_offset], Prof_IntraGC);
								}

								RSP_write_ops[write_plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, write_plane * RSP_BYTES_PER_PAGE);
								RSP_write_ops[write_plane].pSpareData = NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][0];
								RSP_write_ops[write_plane].nChannel = (RSP_UINT8) channel;
								RSP_write_ops[write_plane].nBank = (RSP_UINT8) bank;
								RSP_write_ops[write_plane].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + write_plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
								RSP_write_ops[write_plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + write_plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
								RSP_write_ops[write_plane].m_anVPN[0] = plane_ppn[0];
								RSP_write_ops[write_plane].m_anVPN[1] = plane_ppn[1];
								RSP_write_ops[write_plane].m_anLPN[0] = NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][0][REQ_LPN];
								RSP_write_ops[write_plane].m_anLPN[1] = NAND_bank_state[channel][bank].GCbuf_lpn[write_plane][1][REQ_LPN];
							}
							m_pVFLWrapper->INC_PROGRAMPENDING();
							m_pVFLWrapper->MetaIssue(RSP_write_ops);
							m_pVFLWrapper->WAIT_PROGRAMPENDING();
							m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
							
							block = gc_block;
							NAND_bank_state[channel][bank].GCbuf_index = 0;
							set_vcount(channel, bank, block, get_vcount(channel, bank, block) + PLANES_PER_BANK * LPAGE_PER_PPAGE);
						}
						cpy_cnt++;
					}
					if (vcount == cpy_cnt)
						break;
				}
				if (vcount == cpy_cnt)
					break;
			}
			if (vcount == cpy_cnt)
				break;
		}
		RSP_ASSERT(vcount == cpy_cnt);

		if (NAND_bank_state[channel][bank].GCbuf_index)
		{
			to_write = NAND_bank_state[channel][bank].GCbuf_index;
			new_vpn = free_vpn++;
			super_blk = new_vpn / PAGES_PER_BLK;
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				for (buf_offset = 0; buf_offset < LPAGE_PER_PPAGE; buf_offset++)
				{
					plane_ppn[buf_offset] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset;
					plane_lpn[buf_offset] = RSP_INVALID_LPN;
					if (plane * LPAGE_PER_PPAGE + buf_offset < to_write)
					{
						set_vpn(NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][REQ_LPN], plane_ppn[buf_offset], Prof_IntraGC);
						plane_lpn[buf_offset] = NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][REQ_LPN];
					}
					else{
						NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][REQ_LPN] = RSP_INVALID_LPN;
						NAND_bank_state[channel][bank].GCbuf_lpn[plane][buf_offset][REMAP_LPN] = RSP_INVALID_LPN;
					}
				}

				RSP_write_ops[plane].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, plane * RSP_BYTES_PER_PAGE);
				RSP_write_ops[plane].pSpareData = NAND_bank_state[channel][bank].GCbuf_lpn[plane][0];
				RSP_write_ops[plane].nChannel = (RSP_UINT8) channel;
				RSP_write_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_write_ops[plane].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
				RSP_write_ops[plane].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
				RSP_write_ops[plane].m_anVPN[0] = plane_ppn[0];
				RSP_write_ops[plane].m_anVPN[1] = plane_ppn[1];
				RSP_write_ops[plane].m_anLPN[0] = plane_lpn[0];
				RSP_write_ops[plane].m_anLPN[1] = plane_lpn[1];

			}
			m_pVFLWrapper->INC_PROGRAMPENDING();
			m_pVFLWrapper->MetaIssue(RSP_write_ops);
			m_pVFLWrapper->WAIT_PROGRAMPENDING();
			block = gc_block;
			NAND_bank_state[channel][bank].GCbuf_index = 0;
			set_vcount(channel, bank, block, get_vcount(channel, bank, block) + to_write);
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_write, to_write);
		}
		//update metadata
		set_vcount(channel, bank, vt_block, (RSP_UINT32)VC_MAX);
		set_new_write_vpn(channel, bank, free_vpn);

		del_blk_from_list(channel, bank, vt_block, &NAND_bank_state[channel][bank].data_list);
		set_gc_block(channel, bank, vt_block);
		inc_free_blk(channel, bank);

		insert_bl_tail(channel, bank, gc_block, &NAND_bank_state[channel][bank].data_list);

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
			RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
			RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(vt_block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
		}
		m_pVFLWrapper->INC_ERASEPENDING();
		m_pVFLWrapper->Issue(RSP_erase_ops);
		m_pVFLWrapper->WAIT_ERASEPENDING();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_IntraGC_erase, 1);

		return;
	}

	RSP_UINT32 ATLWrapper::inter_GC(RSP_UINT32 channel, RSP_UINT32 bank){

		RSP_UINT32 vt_block, gc_block, loop = 0; //victim block and gc block, vt_block can be plural
		RSP_UINT32 free_vpn_idx; //super page level
		RSPReadOp RSP_read_op;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];

		m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_num, 1);
		
		free_vpn_idx = NAND_bank_state[channel][bank].cur_write_vpn;
		RSP_ASSERT(NAND_bank_state[channel][bank].free_list.size == 0);
		//page_per_blk level iterator
		while (NAND_bank_state[channel][bank].free_list.size == 0){
			RSP_UINT32 vcount, cpy_cnt = 0;
			loop++;
			vt_block = get_vt_vblock(channel, bank);
			vcount = get_vcount(channel, bank, vt_block);
			del_blk_from_list(channel, bank, vt_block, &NAND_bank_state[channel][bank].data_list);

			for (int page_iter = 0; page_iter < PAGES_PER_BLK; page_iter++){
				//plane level iterator
				for (int plane_iter = 0; plane_iter < PLANES_PER_BANK; plane_iter++){
					for (int high_low_iter = 0; high_low_iter < LPAGE_PER_PPAGE; high_low_iter++){
						RSP_UINT32 old_vpn = vt_block * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + page_iter;
						RSP_UINT32 spare_area[LPAGE_PER_PPAGE * NUM_SPARE_LPN];
						RSP_UINT32 spare_lpn, read_vpn;

						/*if (high_low_iter){
							RSP_read_op.pData = (RSP_UINT32 *)sub_addr(
								add_addr(
								NAND_bank_state[channel][bank].GCbuf_addr,
								NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE)),
								RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
						}
						else{
							RSP_read_op.pData = (RSP_UINT32 *)add_addr(
								NAND_bank_state[channel][bank].GCbuf_addr,
								NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
						}*/
						if (!high_low_iter){
							RSP_read_op.pData = (RSP_UINT32 *)add_addr(
								NAND_bank_state[channel][bank].GCbuf_addr,
								NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
							RSP_read_op.nReqID = RSP_INVALID_RID;
							RSP_read_op.nChannel = (RSP_UINT8) channel;
							RSP_read_op.nBank = (RSP_UINT8) bank;
							RSP_read_op.nBlock = (RSP_UINT16) get_block(old_vpn);
							RSP_read_op.nPage = get_page_offset(old_vpn);
							RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane_iter, RSP_read_op.nPage, high_low_iter);
							m_pVFLWrapper->INC_READPENDING();
							m_pVFLWrapper->MetaIssue(RSP_read_op);
							m_pVFLWrapper->WAIT_READPENDING();
							m_pVFLWrapper->_GetSpareData(spare_area);
							m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_read, 1);
						}

						if (spare_area[high_low_iter * LPAGE_PER_PPAGE + 1] != RSP_INVALID_LPN)
							spare_lpn = spare_area[high_low_iter * LPAGE_PER_PPAGE + 1];
						else
							spare_lpn = spare_area[high_low_iter * LPAGE_PER_PPAGE];

						if (spare_lpn == RSP_INVALID_LPN)
							continue;

						//spare_lpn = spare_area[high_low_iter * LPAGE_PER_PPAGE];
						read_vpn = get_vpn(spare_lpn, Prof_InterGC);
						old_vpn += ((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK);

						RSP_ASSERT(spare_lpn < NUM_LBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE);
						RSP_ASSERT(read_vpn < NUM_PBLK * PAGES_PER_BLK * LPAGE_PER_PPAGE || read_vpn == VC_MAX || is_in_write_buffer(read_vpn));

						if (old_vpn * LPAGE_PER_PPAGE + high_low_iter == read_vpn){
							//valid page
							RSP_UINT32 new_plane = NAND_bank_state[channel][bank].GCbuf_index / LPAGE_PER_PPAGE;
							RSP_UINT32 buf_offset = NAND_bank_state[channel][bank].GCbuf_index % LPAGE_PER_PPAGE;

							NAND_bank_state[channel][bank].GCbuf_index++;
							NAND_bank_state[channel][bank].GCbuf_lpn[new_plane][buf_offset][REQ_LPN] = spare_lpn;
							NAND_bank_state[channel][bank].GCbuf_lpn[new_plane][buf_offset][REMAP_LPN] = RSP_INVALID_LPN;

							if (NAND_bank_state[channel][bank].GCbuf_index == PLANES_PER_BANK * LPAGE_PER_PPAGE){
								RSP_UINT32 super_blk, plane_ppn[LPAGE_PER_PPAGE];

								if (free_vpn_idx % PAGES_PER_BLK == PAGES_PER_BLK - 1){
									gc_block = get_gc_block(channel, bank);
									set_vcount(channel, bank, gc_block, 0);
									free_vpn_idx = gc_block * PAGES_PER_BLK;
									insert_bl_tail(channel, bank, gc_block, &NAND_bank_state[channel][bank].data_list);
								}
								else{
									free_vpn_idx++;
								}

								super_blk = free_vpn_idx / PAGES_PER_BLK;
								for (RSP_UINT32 cpy_plane_iter = 0; cpy_plane_iter < PLANES_PER_BANK; cpy_plane_iter++){
									for (RSP_UINT32 buf_offset_iter = 0; buf_offset_iter < LPAGE_PER_PPAGE; buf_offset_iter++){
										plane_ppn[buf_offset_iter] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK)
											+ super_blk * PLANES_PER_BANK * PAGES_PER_BLK
											+ cpy_plane_iter * PAGES_PER_BLK
											+ free_vpn_idx % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset_iter;
										set_vpn(NAND_bank_state[channel][bank].GCbuf_lpn[cpy_plane_iter][buf_offset_iter][REQ_LPN], plane_ppn[buf_offset_iter], Prof_InterGC);
									}

									RSP_write_ops[cpy_plane_iter].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, cpy_plane_iter * RSP_BYTES_PER_PAGE);
									RSP_write_ops[cpy_plane_iter].pSpareData = NAND_bank_state[channel][bank].GCbuf_lpn[cpy_plane_iter][0];
									RSP_write_ops[cpy_plane_iter].nChannel = (RSP_UINT8) channel;
									RSP_write_ops[cpy_plane_iter].nBank = (RSP_UINT8) bank;
									RSP_write_ops[cpy_plane_iter].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + cpy_plane_iter * PAGES_PER_BLK + free_vpn_idx % PAGES_PER_BLK);
									RSP_write_ops[cpy_plane_iter].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + cpy_plane_iter * PAGES_PER_BLK + free_vpn_idx % PAGES_PER_BLK);
									RSP_write_ops[cpy_plane_iter].m_anVPN[0] = plane_ppn[0];
									RSP_write_ops[cpy_plane_iter].m_anVPN[1] = plane_ppn[1];
									RSP_write_ops[cpy_plane_iter].m_anLPN[0] = NAND_bank_state[channel][bank].GCbuf_lpn[cpy_plane_iter][0][REQ_LPN];
									RSP_write_ops[cpy_plane_iter].m_anLPN[1] = NAND_bank_state[channel][bank].GCbuf_lpn[cpy_plane_iter][1][REQ_LPN];
								}
								m_pVFLWrapper->INC_PROGRAMPENDING();
								m_pVFLWrapper->MetaIssue(RSP_write_ops);
								m_pVFLWrapper->WAIT_PROGRAMPENDING();

								NAND_bank_state[channel][bank].GCbuf_index = 0;
								set_vcount(channel, bank, super_blk, get_vcount(channel, bank, super_blk) + PLANES_PER_BANK * LPAGE_PER_PPAGE);
								m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_write, PLANES_PER_BANK * LPAGE_PER_PPAGE);
							}
							cpy_cnt++;
						}
						if (vcount == cpy_cnt) break;
					}
					if (vcount == cpy_cnt) break;
				}
				if (vcount == cpy_cnt) break;
			}
			RSP_ASSERT(vcount == cpy_cnt);
		
			//remained page (not super)
			if (NAND_bank_state[channel][bank].GCbuf_index)
			{
				RSP_UINT32 to_write, super_blk, plane_ppn[LPAGE_PER_PPAGE], plane_lpn[LPAGE_PER_PPAGE];
				to_write = NAND_bank_state[channel][bank].GCbuf_index;

				if (free_vpn_idx % PAGES_PER_BLK == PAGES_PER_BLK - 1){
					gc_block = get_gc_block(channel, bank);
					set_vcount(channel, bank, gc_block, 0);
					free_vpn_idx = gc_block * PAGES_PER_BLK;
					insert_bl_tail(channel, bank, gc_block, &NAND_bank_state[channel][bank].data_list);
				}
				else{
					free_vpn_idx++;
				}
				super_blk = free_vpn_idx / PAGES_PER_BLK;
				for (RSP_UINT32 plane_iter = 0; plane_iter < PLANES_PER_BANK; plane_iter++)
				{
					for (RSP_UINT32 buf_offset_iter = 0; buf_offset_iter < LPAGE_PER_PPAGE; buf_offset_iter++)
					{
						plane_ppn[buf_offset_iter] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + free_vpn_idx % PAGES_PER_BLK) * LPAGE_PER_PPAGE + buf_offset_iter;
						plane_lpn[buf_offset_iter] = RSP_INVALID_LPN;
						if (plane_iter * LPAGE_PER_PPAGE + buf_offset_iter < to_write)
						{
							set_vpn(NAND_bank_state[channel][bank].GCbuf_lpn[plane_iter][buf_offset_iter][REQ_LPN], plane_ppn[buf_offset_iter], Prof_InterGC);
							plane_lpn[buf_offset_iter] = NAND_bank_state[channel][bank].GCbuf_lpn[plane_iter][buf_offset_iter][REQ_LPN];
						}
						else{
							NAND_bank_state[channel][bank].GCbuf_lpn[plane_iter][buf_offset_iter][REQ_LPN] = RSP_INVALID_LPN;
							NAND_bank_state[channel][bank].GCbuf_lpn[plane_iter][buf_offset_iter][REMAP_LPN] = RSP_INVALID_LPN;
						}
		
					}

					RSP_write_ops[plane_iter].pData = (RSP_UINT32*)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, plane_iter * RSP_BYTES_PER_PAGE);
					RSP_write_ops[plane_iter].pSpareData = NAND_bank_state[channel][bank].GCbuf_lpn[plane_iter][0];
					RSP_write_ops[plane_iter].nChannel = (RSP_UINT8) channel;
					RSP_write_ops[plane_iter].nBank = (RSP_UINT8) bank;
					RSP_write_ops[plane_iter].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + free_vpn_idx % PAGES_PER_BLK);
					RSP_write_ops[plane_iter].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + free_vpn_idx % PAGES_PER_BLK);
					RSP_write_ops[plane_iter].m_anVPN[0] = plane_ppn[0];
					RSP_write_ops[plane_iter].m_anVPN[1] = plane_ppn[1];
					RSP_write_ops[plane_iter].m_anLPN[0] = plane_lpn[0];
					RSP_write_ops[plane_iter].m_anLPN[1] = plane_lpn[1];

				}
				m_pVFLWrapper->INC_PROGRAMPENDING();
				m_pVFLWrapper->MetaIssue(RSP_write_ops);
				m_pVFLWrapper->WAIT_PROGRAMPENDING();

				NAND_bank_state[channel][bank].GCbuf_index = 0;
				set_vcount(channel, bank, super_blk, get_vcount(channel, bank, super_blk) + to_write);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_write, to_write);
			}

			for (RSP_UINT32 plane_iter = 0; plane_iter < PLANES_PER_BANK; plane_iter++)
			{
				RSP_erase_ops[plane_iter].nChannel = (RSP_UINT8) channel;
				RSP_erase_ops[plane_iter].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane_iter].nBlock = (RSP_UINT16) get_block(vt_block * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);
			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_InterGC_erase, 1);

			set_vcount(channel, bank, vt_block, (RSP_UINT32)VC_MAX);
		
			//if the superblock of free_vpn_idx is current gc_block, then the vt_block should be new gc_block.
			//if else, it means that gc_block slot was already filled with previous victim block, thus current victim can go to free block list.
			if (get_gc_block(channel, bank) == get_block(free_vpn_idx)){
				set_gc_block(channel, bank, vt_block);
			}
			else{
				set_vcount(channel, bank, vt_block, 0);
				insert_bl_tail(channel, bank, vt_block, &NAND_bank_state[channel][bank].free_list);
			}
			set_new_write_vpn(channel, bank, free_vpn_idx);
		}

		return get_free_block(channel, bank);
	}

	//need delete after calling this function
	RSP_UINT32 ATLWrapper::get_free_block(RSP_UINT32 channel, RSP_UINT32 bank){
	
		if (NAND_bank_state[channel][bank].free_list.size == 0)
			return 0;
		else
			return NAND_bank_state[channel][bank].free_list.head->block_no;
	}

	RSP_UINT32 ATLWrapper::get_vt_vblock(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32 temp_vcount = PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE, result_block = 0, loop, block, vcount;
		block_struct *blk_tmp = NAND_bank_state[channel][bank].data_list.head;
		RSP_UINT32 list_size = NAND_bank_state[channel][bank].data_list.size;

		for (loop = 0; loop < list_size; loop++)
		{
			block = blk_tmp->block_no;
			//if the block is on-going, then skip it
			if ((block == NAND_bank_state[channel][bank].cur_write_vpn / PAGES_PER_BLK) && (NAND_bank_state[channel][bank].cur_write_vpn % PAGES_PER_BLK != PAGES_PER_BLK - 1)){
				blk_tmp = blk_tmp->next;
				continue;
			}
			vcount = get_vcount(channel, bank, block);
			if (temp_vcount > vcount)
			{
				result_block = block;
				temp_vcount = vcount;
			}
			blk_tmp = blk_tmp->next;
		}
		RSP_ASSERT(temp_vcount < PAGES_PER_BLK * PLANES_PER_BANK * LPAGE_PER_PPAGE - 8);
		return result_block;
	}

	RSP_VOID* ATLWrapper::add_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
	{
		return (RSP_VOID *)((RSP_UINT32)start_addr + offset);
	}
	RSP_VOID* ATLWrapper::sub_addr(RSP_VOID* start_addr, RSP_UINT32 offset)
	{
		return (RSP_VOID *)((RSP_UINT32)start_addr - offset);
	}

	RSP_UINT32 ATLWrapper::return_map_ppn(RSP_UINT32 map_offset)
	{
		return MAP_MAPPING_TABLE[map_offset];
	}

	//block list manager

	RSP_VOID ATLWrapper::insert_bl_tail(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, block_struct_head *list_head){

		if (list_head->size == 0)
		{
			list_head->head = &NAND_bank_state[channel][bank].block_list[block];
			list_head->head->before = list_head->head;
			list_head->head->next = list_head->head;
		}
		else
		{
			block_struct *temp = &NAND_bank_state[channel][bank].block_list[block];
			temp->before = list_head->head->before;
			temp->next = list_head->head;
			list_head->head->before->next = temp;
			list_head->head->before = temp;
		
		}
		list_head->size++;
	}

	RSP_VOID ATLWrapper::del_blk_from_list(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, block_struct_head *list_head){

		RSP_ASSERT(list_head->size);

		if (list_head->head->block_no == block)
		{
			block_struct *temp = &NAND_bank_state[channel][bank].block_list[block];
			temp->before->next = temp->next;
			temp->next->before = temp->before;
			list_head->head = temp->next;
		}
		else
		{
			block_struct *temp = &NAND_bank_state[channel][bank].block_list[block];
			temp->before->next = temp->next;
			temp->next->before = temp->before;
		}
		list_head->size--;

	}

	RSP_VOID ATLWrapper::flush(RSP_VOID)
	{
		for (RSP_UINT32 channel = 0; channel < NAND_NUM_CHANNELS; channel++){
			for (RSP_UINT32 bank = 0; bank < BANKS_PER_CHANNEL; bank++){
				for (RSP_UINT8 write_type = 0; write_type < WRITE_TYPE_NUM; write_type++){
					write_buffer_flush(channel, bank, write_type);
				}
			}
		}
		map_flush();
		meta_flush();
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_num_flush, 1);
	}
	RSP_VOID ATLWrapper::flush_bank(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		/*for (RSP_UINT32 write_type = 0; write_type < WRITE_TYPE_NUM; write_type++){
			write_buffer_flush(channel, bank, write_type);
		}*/
		map_flush();
		bank_meta_flush(channel, bank);
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_num_flush, 1);

	}

	RSP_VOID ATLWrapper::write_buffer_flush(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT8 WRITE_TYPE){
		//be careful about managing vcount and remained_count for blk struct.

		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSP_UINT32 new_vpn, plane_ppn[LPAGE_PER_PPAGE], plane_lpn[LPAGE_PER_PPAGE];
		RSP_UINT32 vcount = 0;
		RSP_UINT32 idx_iter = 0;
		RSP_UINT32 super_blk;

		dbg1 = NAND_NUM_CHANNELS;
		dbg2 = BANKS_PER_CHANNEL;
		dbg3 = BLKS_PER_PLANE;
		
		if(NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] == 0x00){
			return;
		}

		if (WRITE_TYPE == SHRD_SW)
			new_vpn = assign_new_write_vpn(channel, bank);
		else if (WRITE_TYPE == SHRD_RW)
			new_vpn = assign_new_write_vpn_RW(channel, bank);
		else
			new_vpn = assign_new_write_vpn_JN(channel, bank);

		RSP_ASSERT(new_vpn < BLKS_PER_BANK * PAGES_PER_BLK);

		super_blk = new_vpn / PAGES_PER_BLK;

		for (RSP_UINT32 plane_iter = 0; plane_iter < PLANES_PER_BANK; plane_iter++){
			for (RSP_UINT32 lpage_iter = 0; lpage_iter < LPAGE_PER_PPAGE; lpage_iter++){

				plane_ppn[lpage_iter] = (((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK) + super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK) * LPAGE_PER_PPAGE + lpage_iter;
				plane_lpn[lpage_iter] = RSP_INVALID_LPN;
				if ((NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] >> (plane_iter * LPAGE_PER_PPAGE + lpage_iter)) & 0x1){
					//writebuf_bitmap checking.
					vcount++;
					RSP_ASSERT(NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][lpage_iter][REQ_LPN] != RSP_INVALID_LPN);
					set_vpn(NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][lpage_iter][REQ_LPN], plane_ppn[lpage_iter], WRITE_TYPE);
					plane_lpn[lpage_iter] = NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][lpage_iter][REQ_LPN];
					RSP_ASSERT(NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][plane_iter] != NULL);
				}
				else{
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][lpage_iter][REQ_LPN] = RSP_INVALID_LPN;
					NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][lpage_iter][REMAP_LPN] = RSP_INVALID_LPN;
				}

			}

			//dbg
			//dbg_buffflush[dbg_buffflush_ptr] = NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][plane_iter];
			//dbg_buffflush_ptr++;
			//dbgend

			RSP_write_ops[plane_iter].pData = NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][plane_iter];
			RSP_write_ops[plane_iter].pSpareData = NAND_bank_state[channel][bank].writebuf_lpn[WRITE_TYPE][plane_iter][0];
			RSP_write_ops[plane_iter].nChannel = (RSP_UINT8) channel;
			RSP_write_ops[plane_iter].bmpTargetSector = NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][plane_iter];
			RSP_write_ops[plane_iter].nBank = (RSP_UINT8) bank;
			RSP_write_ops[plane_iter].nBlock = (RSP_UINT16) get_block(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane_iter].nPage = get_page_offset(super_blk * PLANES_PER_BANK * PAGES_PER_BLK + plane_iter * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane_iter].m_anVPN[0] = plane_ppn[0];
			RSP_write_ops[plane_iter].m_anVPN[1] = plane_ppn[1];
			RSP_write_ops[plane_iter].m_anLPN[0] = plane_lpn[0];
			RSP_write_ops[plane_iter].m_anLPN[1] = plane_lpn[1];

		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		if (WRITE_TYPE == SHRD_RW)
			m_pVFLWrapper->Issue(RSP_write_ops);
		else{
			m_pVFLWrapper->MetaIssue(RSP_write_ops);
		}
		//m_pVFLWrapper->Issue(RSP_write_ops);
		m_pVFLWrapper->WAIT_PROGRAMPENDING();

		//NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE] = 0;
		set_vcount(channel, bank, super_blk, get_vcount(channel, bank, super_blk) + vcount);

		NAND_bank_state[channel][bank].writebuf_index[WRITE_TYPE] = 0;
		NAND_bank_state[channel][bank].writebuf_bitmap[WRITE_TYPE] = 0;
		
		for (RSP_UINT8 iter = 0; iter < PLANES_PER_BANK; iter++){
			NAND_bank_state[channel][bank].writebuf_addr[WRITE_TYPE][iter] = NULL;
			NAND_bank_state[channel][bank].writebuf_addr_bitmap[WRITE_TYPE][iter] = 0;
		}

		if(WRITE_TYPE == SHRD_SW)
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_SW_flush, vcount);
		else if(WRITE_TYPE == SHRD_RW)
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_RW_flush, vcount);
		else
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_JN_flush, vcount);
		if (WRITE_TYPE != SHRD_SW){
			NAND_bank_state[channel][bank].block_list[super_blk].remained_remap_cnt += vcount;
		}
	}

	RSP_VOID ATLWrapper::map_flush(RSP_VOID)
	{
		RSP_UINT32 loop;
		for (loop = 0; loop < NUM_CACHED_MAP; loop++)
		{
			if (CACHE_MAP_DIRTY_TABLE[loop])
			{
				CACHE_MAP_DIRTY_TABLE[loop] = false;
				map_write(CACHE_MAPPING_TABLE[loop], loop);
				m_pVFLWrapper->RSP_INC_ProfileData(Prof_map_flush, 1);
			}
		}
	}

	RSP_VOID ATLWrapper::meta_flush(RSP_VOID)
	{
		RSP_UINT32 bank,  channel;

		for (channel = 0; channel < NAND_NUM_CHANNELS; channel++)
		{
			for (bank = 0; bank < BANKS_PER_CHANNEL; bank++)
			{
				bank_meta_flush(channel, bank);
			}
		}
	}
	RSP_VOID ATLWrapper::bank_meta_flush(RSP_UINT32 channel, RSP_UINT32 bank)
	{
		RSP_UINT32  plane,  block, new_vpn;
		RSPProgramOp RSP_write_ops[PLANES_PER_BANK];
		RSPEraseOp RSP_erase_ops[PLANES_PER_BANK];
		new_vpn = NAND_bank_state[channel][bank].cur_meta_vpn;
		block = NAND_bank_state[channel][bank].meta_blk;
		if (new_vpn == 0 && NAND_bank_state[channel][bank].meta_start == true)
		{
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				RSP_erase_ops[plane].nChannel = (RSP_UINT8) channel;
				RSP_erase_ops[plane].nBank = (RSP_UINT8) bank;
				RSP_erase_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK);
			}
			m_pVFLWrapper->INC_ERASEPENDING();
			m_pVFLWrapper->Issue(RSP_erase_ops);

			m_pVFLWrapper->WAIT_ERASEPENDING();
			m_pVFLWrapper->RSP_INC_ProfileData(Prof_meta_erase, 1);
		}

		for (plane = 0; plane < PLANES_PER_BANK; plane++)
		{
			RSP_write_ops[plane].pData = NAND_bank_state[channel][bank].cpybuf_addr;
			RSP_write_ops[plane].pSpareData = NULL_SPARE;
			RSP_write_ops[plane].nChannel = (RSP_UINT8) channel;
			RSP_write_ops[plane].nBank = (RSP_UINT8) bank;
			RSP_write_ops[plane].nBlock = (RSP_UINT16) get_block(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane].nPage = get_page_offset(block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + new_vpn % PAGES_PER_BLK);
			RSP_write_ops[plane].m_anVPN[0] = generate_vpn(channel, bank, block, plane, new_vpn, 0);
			RSP_write_ops[plane].m_anVPN[1] = generate_vpn(channel, bank, block, plane, new_vpn, 1);
			RSP_write_ops[plane].m_anLPN[0] = RSP_INVALID_LPN;
			RSP_write_ops[plane].m_anLPN[1] = RSP_INVALID_LPN;
		}
		m_pVFLWrapper->INC_PROGRAMPENDING();
		m_pVFLWrapper->MetaIssue(RSP_write_ops);
		m_pVFLWrapper->WAIT_PROGRAMPENDING();
		NAND_bank_state[channel][bank].cur_meta_vpn = (NAND_bank_state[channel][bank].cur_meta_vpn + 1) % PAGES_PER_BLK;
		NAND_bank_state[channel][bank].meta_start = true;
		m_pVFLWrapper->RSP_INC_ProfileData(Prof_meta_flush, 1);

	}

/*
	RSP_UINT32 ATLWrapper::vcount_test(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
	{

		RSP_UINT32 src_page, plane, high_low, old_vpn, vcount = get_vcount(channel, bank, block);
		RSP_UINT32 spare_area[4], read_vpn_RQ, read_vpn_RM, cpy_cnt = 0;
		RSPReadOp RSP_read_op;
		for (src_page = 0; src_page < PAGES_PER_BLK; src_page++)
		{
			for (plane = 0; plane < PLANES_PER_BANK; plane++)
			{
				for (high_low = 0; high_low < LPAGE_PER_PPAGE; high_low++)
				{
					old_vpn = block * PLANES_PER_BANK * PAGES_PER_BLK + plane * PAGES_PER_BLK + src_page % PAGES_PER_BLK;
					if (high_low)
						RSP_read_op.pData = (RSP_UINT32 *)sub_addr(add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE)), RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE);
					else
						RSP_read_op.pData = (RSP_UINT32 *)add_addr(NAND_bank_state[channel][bank].GCbuf_addr, NAND_bank_state[channel][bank].GCbuf_index * (RSP_BYTES_PER_PAGE / LPAGE_PER_PPAGE));
					RSP_read_op.nReqID = RSP_INVALID_RID;
					RSP_read_op.nChannel = channel;
					//RSP_read_op.pSpareData = spare_area;
					RSP_read_op.nBank = bank;
					RSP_read_op.nBlock = get_block(old_vpn);
					RSP_read_op.nPage = get_page_offset(old_vpn);
					if (high_low)
						RSP_read_op.bmpTargetSector = 0xff00;
					else
						RSP_read_op.bmpTargetSector = 0xff;
					RSP_read_op.m_nVPN = generate_vpn(channel, bank, RSP_read_op.nBlock, plane, RSP_read_op.nPage, high_low);

					m_pVFLWrapper->Issue(RSP_read_op);
					m_pVFLWrapper->INC_READPENDING();


					m_pVFLWrapper->WAIT_READPENDING();
					m_pVFLWrapper->_GetSpareData(spare_area);
					if (spare_area[high_low * 2] != RSP_INVALID_LPN)
						read_vpn_RQ = get_vpn(spare_area[high_low * 2]);
					if (spare_area[high_low * LPAGE_PER_PPAGE + 1] != RSP_INVALID_LPN)
						read_vpn_RM = get_vpn(spare_area[high_low * LPAGE_PER_PPAGE + 1]);
					map_vcount_test(1, 1, 0);

					old_vpn += ((channel * BANKS_PER_CHANNEL + bank) * PAGES_PER_BANK);
					//Read complete
					if (old_vpn * LPAGE_PER_PPAGE + high_low == read_vpn_RQ)
					{
						cpy_cnt++;
					}
					else if (spare_area[high_low * LPAGE_PER_PPAGE + 1] != RSP_INVALID_LPN && old_vpn * LPAGE_PER_PPAGE + high_low == read_vpn_RM)
						cpy_cnt++;
				}

			}
		}
		if (vcount > cpy_cnt && !(vcount == VC_MAX && cpy_cnt == 0))
		{
			RSP_ASSERT(0);
		}
		return 1;
	}

	RSP_UINT32 ATLWrapper::map_vcount_test(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block)
	{
		RSP_UINT32 old_vpn, victim_blk = 0, vcount = get_map_vcount(channel, bank, block), i, copy_cnt = 0, src_lpn;
		victim_blk = block;

		for (i = 0; i < PAGES_PER_BLK; i++)
		{
			src_lpn = MAPP2L[((channel * BANKS_PER_CHANNEL + bank) * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK) + victim_blk * PAGES_PER_BLK + i];
			if (src_lpn == VC_MAX)
				continue;
			old_vpn = MAP_MAPPING_TABLE[src_lpn];
			RSP_ASSERT(old_vpn < PAGES_PER_BLK * MAP_ENTRY_BLK_PER_BANK || old_vpn == VC_MAX);
			RSP_ASSERT(src_lpn < NAND_NUM_CHANNELS * BANKS_PER_CHANNEL * MAP_ENTRY_BLK_PER_BANK * PAGES_PER_BLK || old_vpn == VC_MAX);

			if (old_vpn == (RSP_UINT32)VC_MAX)
				continue;
			if (old_vpn == victim_blk * PAGES_PER_BLK + i)
			{
				//WRITE_complete

				copy_cnt++;
			}
			if (copy_cnt == vcount)
				break;
		}
		RSP_ASSERT(copy_cnt == vcount || (vcount == VC_MAX && copy_cnt == 0));
		return true;
	}*/
}
