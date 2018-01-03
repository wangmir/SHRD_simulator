#ifndef __ATLWrapper_H__
#define __ATLWrapper_H__

//#define __NAND_virtual_H
#ifdef __NAND_virtual_H
#include "NAND_virt.h"
#else
#include "RSP_Header.h"
#include "RSP_OSAL.h"

#include "VFLWrapper.h"
#endif

typedef unsigned long RSP_UINT32;

//#define Hesper_DEBUG

namespace Hesper
{
	//V2P table Management
#define L2V_MAX_ENTRY (100000)
#define MAX_PENDDING_VC (100000)


#define FLUSH_BANKS_COUNTER (10)
//#define ATL_ASSERTION_TEST
//#define WAIT_TEST
	class ATLWrapper;

	struct LPN_list
	{
		RSP_UINT32 lpn;
		RSP_UINT32 next;
		RSP_UINT32 offset;
	};
	struct LPN_head
	{
		RSP_UINT32 count;
		RSP_UINT32  head;
	};
	
	enum MAP_PAGE_TYPE{
		L2P,
		P2L,
	};

	enum ATL_INITIALIZE_TYPE{
		SEQ80,
		SEQ80_RAND50_30,
		SEQ50_RAND50_80,
		RAND_50_110,
		RAND_80_110,
	};
	enum FLUSH_TYPE{
		NO_FLUSH,
		META_FLUSH,
		ALL_FLUSH,
	};

	enum ReadState{
		ReadWriteBuffer,
		ReadError,
		ReadNand,
	};
	enum MapAccessType{
		Prof_Read,
		Prof_Write,
		Prof_FGC,
		Prof_BGC,
		Prof_Remap,
		Prof_Flush,
		Prof_Trim,
		Prof_Type_total_Num
	};

	enum Profile{
		//Host_Read_Write
		Prof_Host_write,
		Prof_Host_read,

		//NAND_Read_Write
		Prof_NAND_write_4KB_page,
		Prof_NAND_read,
		Prof_NAND_erase,

		//Read_page
		Prof_Read_read,
		Prof_Read_Map_log,
		Prof_Read_Map_load,

		//Write_page
		Prof_Write_write,
		Prof_Write_Map_log,				//10
		Prof_Write_Map_load,

		//FGC
		Prof_FGC_Map_log,
		Prof_FGC_Map_load,
		Prof_FGC_write,
		Prof_FGC_read,
		Prof_FGC_erase,

		//BGC
		Prof_BGC_Map_log, 
		Prof_BGC_Map_load,
		Prof_BGC_write,
		Prof_BGC_read,					//20
		Prof_BGC_erase,

		//L2P
		Prof_L2P_Total_log,
		Prof_L2P_Total_load,
		Prof_L2P_GC_read,
		Prof_L2P_GC_write,
		Prof_L2P_erase,
		Prof_L2P_HIT,  
		Prof_L2P_MISS,

		//P2L
		Prof_P2L_Total_log,
		Prof_P2L_Total_load,			//30
		Prof_P2L_GC_read,
		Prof_P2L_GC_write,
		Prof_P2L_erase,
		Prof_P2L_HIT,  
		Prof_P2L_MISS,

		//VC/VM
		Prof_Remap_command_count,
		Prof_Remap_by_FTL,
		Prof_Remap_by_HIL,
		Prof_Remap_Realcopy_write,
		Prof_Remap_Struct_write,		//40
		Prof_Remap_Map_log,
		Prof_Remap_Map_load,			
		Prof_Remap_VC_count,
		Prof_Remap_VM_count,
		Prof_Remap_VC_Realcopy_count,
		Prof_Remap_VM_Realcopy_count,
		Prof_Remap_VC_Intercore_count,
		Prof_Remap_VM_Intercore_count,
		Prof_Remap_flush,

		//Valid_Bitmap	
		Prof_VB_write,					//50

		//Flush
		Prof_num_flush,
		Prof_num_Bank_flush,			
		Prof_meta_flush,
		Prof_Bank_meta_flush,
		Prof_meta_erase,
		Prof_buffer_flush_write,
		Prof_L2P_flush_log,
		Prof_P2L_flush_log,
		Prof_LPN_list_log_write,				//60

		//Trim
		Prof_Trim_count,
		Prof_Trim_Map_log,				
		Prof_Trim_Map_load,

		//ETC
		Prof_init_erase,
		Prof_total_num,


	};





#define VC_MAX (0xffffffff)
#define REFCOUNT_MASK (0xffff0000)
#define VCOUNT_MASK (0x0000ffff)
#define REFCOUNT_BIT_OFFSET (16)

#define BIT_PER_RSP_UINT32 (32)
#define BIT_PER_BYTES (8)


#define NUM_FTL_CORE (2)
#define THIS_CORE (__COREID__ - 1) //should be changed into variable
	//Mapping data

#define NUM_MAP_ENTRY(type) (((type * NUM_PBLK + (1- type) * NUM_LBLK) * PAGES_PER_BLK * LPAGE_PER_PPAGE + MAP_ENTRY_SIZE / sizeof_u32 - 1) / (MAP_ENTRY_SIZE / sizeof_u32))
#define NUM_MAP_ENTRY_BLK(type) ((NUM_MAP_ENTRY(type) + PAGES_PER_BLK - 1) / PAGES_PER_BLK)
#define MAP_ENTRY_BLK_PER_BANK(type) ((NUM_MAP_ENTRY_BLK(type) + (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) - 1) / (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) + 10)
#define TOTAL_MAP_BLK(type) (MAP_ENTRY_BLK_PER_BANK(type) * (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS))
#define MAP_ENTRY_SIZE (BYTES_PER_SUPER_PAGE)
#define NUM_MAP_ENTRY_PER_BANK(type) ((NUM_MAP_ENTRY(type) + (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS) - 1) / (BANKS_PER_CHANNEL * NAND_NUM_CHANNELS))
#define NUM_PAGES_PER_MAP (MAP_ENTRY_SIZE / sizeof_u32)

#define ATL_TRUE 1
#define ATL_FALSE 0

	//Macro function
#define get_channel(lpn) ((lpn) % (NAND_NUM_CHANNELS))
#define get_channel_from_ppn(ppn) (((ppn) / (PAGES_PER_BANK * LPAGE_PER_PPAGE)) / BANKS_PER_CHANNEL)
#define get_bank(lpn) ((lpn) / (NAND_NUM_CHANNELS) % BANKS_PER_CHANNEL)
#define get_bank_from_ppn(ppn) (((ppn) / (PAGES_PER_BANK * LPAGE_PER_PPAGE)) % BANKS_PER_CHANNEL)
#define get_plane(page_offset_in_bank) (((page_offset_in_bank) / PAGES_PER_BLK) % PLANES_PER_BANK)
#define get_block(page_offset_in_bank) ((page_offset_in_bank) / PAGES_PER_BLK)
#define get_super_block(page_offset_in_bank) ((page_offset_in_bank) / (PLANES_PER_BANK *  LPAGE_PER_PPAGE * PAGES_PER_BLK))
#define get_page_offset(page_offset_in_bank) ((page_offset_in_bank) % PAGES_PER_BLK)
#define generate_ppn(channel, bank, block, page, high_low) ((((channel) * BANKS_PER_CHANNEL + (bank)) * PAGES_PER_BANK + ((block)) * PAGES_PER_BLK + (page)) * LPAGE_PER_PPAGE + high_low)
#define get_cur_write_ppn(channel, bank) NAND_bank_state[channel][bank].cur_write_ppn
#define inc_free_blk(channel, bank) NAND_bank_state[channel][bank].num_free_blk++;
#define dec_free_blk(channel, bank) NAND_bank_state[channel][bank].num_free_blk--;
#define is_full_bank(channel, bank) (NAND_bank_state[channel][bank].num_free_blk == 0)
#define set_new_write_ppn(channel, bank, write_ppn) NAND_bank_state[channel][bank].cur_write_ppn = write_ppn;
#define get_gc_block(channel, bank) NAND_bank_state[channel][bank].GC_BLK;
#define set_gc_block(channel, bank, blk) NAND_bank_state[channel][bank].GC_BLK = blk;
#define get_modppn_by_ppn(ppn) (((ppn % (PAGES_PER_BANK * LPAGE_PER_PPAGE)) << 3) + (ppn / (PAGES_PER_BANK * LPAGE_PER_PPAGE)))
#define	get_map_offset_by_ppn(ppn) (get_modppn_by_ppn(ppn) / NUM_PAGES_PER_MAP)

#define RSP_ASSERT(bCondition) if (!(bCondition)) {while(1);}
	//write_buffer management
#define WRITE_BUFFER_BIT (0x80000000)
#define VIRTUAL_BIT (0x40000000)
#define REALCOPY_BIT (0xC0000000)
#define is_in_write_buffer(ppn) (RSP_BOOL)((ppn & WRITE_BUFFER_BIT) && !(ppn & VIRTUAL_BIT))
#define is_in_virtual(ppn) (RSP_BOOL)((ppn & VIRTUAL_BIT) && !(ppn & WRITE_BUFFER_BIT))
#define is_in_realcopy(ppn) (RSP_BOOL)((ppn & VIRTUAL_BIT) && (ppn & WRITE_BUFFER_BIT))

#define sizeof_u16 2
#define sizeof_u32 4
#define sizeof_u64 8

#define KB (1024)
#define MB (1024 * KB)

	//RSP_MEM_API
#define rspmalloc(a) RSPOSAL::RSP_MemAlloc(RSPOSAL::DRAM, a)
#define rspsmalloc(a) RSPOSAL::RSP_MemAlloc(RSPOSAL::SRAM, a)

#define BYTES_PER_SECTOR (RSP_BYTE_PER_SECTOR)
#define SECTORS_PER_LPN (RSP_SECTOR_PER_LPN)
#define SECTORS_PER_PAGE (RSP_SECTOR_PER_PAGE)
#define LPAGE_PER_PPAGE (2)
#define PAGES_PER_BLK (RSP_PAGE_PER_BLOCK)
#define RSP_BYTES_PER_PAGE (BYTES_PER_SECTOR * SECTORS_PER_PAGE)

	//For GC test
//#define BLKS_PER_PLANE (RSP_BLOCK_PER_PLANE / 4)
#define BLKS_PER_PLANE (RSP_BLOCK_PER_PLANE)
#define BLKS_PER_BANK (BLKS_PER_PLANE)
#define PLANES_PER_BANK (RSP_NUM_PLANE)
#define BYTES_PER_SUPER_PAGE (RSP_BYTES_PER_PAGE * PLANES_PER_BANK)
#define BANKS_PER_CHANNEL (RSP_NUM_BANK)
#define	NAND_NUM_CHANNELS (RSP_NUM_CHANNEL)
	//for GC test
//	static RSP_UINT32 OP_BLKS = 16;
static RSP_UINT32 OP_BLKS = 1024;
#define OP_BLKS_PER_BANK OP_BLKS / (NAND_NUM_CHANNELS * BANKS_PER_CHANNEL)
	extern RSP_UINT32 NUM_LBLK;
	extern RSP_UINT32 NUM_PBLK;
	extern RSP_UINT32 CMT_size;
#define PAGES_PER_BANK (PAGES_PER_BLK * BLKS_PER_PLANE * PLANES_PER_BANK)

#define NUM_META_BLKS (1)
#define NUM_SPARE_LPN (2)

#define OADDR (0)
#define TADDR (1)

#define BGC_THRESHOLD (BLKS_PER_PLANE * 0.2)


#define SPARE_LPNS (3)
	struct BLK_STRUCT
	{
		RSP_UINT32 block_offset;
		BLK_STRUCT* before;
		BLK_STRUCT* next;
	};
	struct free_blk_head
	{
		RSP_UINT32 count;
		BLK_STRUCT* head;
	};
	struct NAND_bankstat
	{
		RSP_UINT32 GC_BLK;
		RSP_UINT32 cur_write_ppn;
		RSP_UINT32 cur_map_ppn[2];
		RSP_UINT32* MAP_blk_offset[2];
		RSP_UINT32 MAP_GC_BLK[2];
		RSP_UINT32 VALID_BITMAP_PAGE;
		RSP_UINT32 cur_meta_ppn;
		RSP_UINT32 meta_blk;
		RSP_UINT32* cpybuf_addr[2];

		free_blk_head free_blk_list;
		free_blk_head map_blk_list[2];
		BLK_STRUCT* blk_list;

		RSP_UINT32 MAP_GC_victim_blk[2];
		RSP_UINT32 MAP_GC_src_page[2];
		RSP_UINT32 MAP_GC_free_page[2];

		RSP_UINT32 GC_victim_blk;
		RSP_UINT32 GC_src_page;
		RSP_UINT32 GC_plane;
		RSP_UINT32 GC_free_page;

		RSP_UINT32* GCbuf_addr;
		RSP_UINT32 GCbuf_index;
		RSP_UINT32 GCbuf_lpn[PLANES_PER_BANK][LPAGE_PER_PPAGE][SPARE_LPNS];
		RSP_BOOL write_start;
		RSP_BOOL map_start[2];
		RSP_BOOL meta_start;

		RSP_UINT8 cpybuf_index;

	};
	

	static RSP_UINT32 NULL_SPARE[4] = {
		0x00,
		0x00,
		0x00,
		0x00
	};

//#define memory_barrier() asm volatile ("" : : : "memory");
#define TOTAL_SM_VALUES (55)
	struct SM_struct
	{
		volatile RSP_UINT32 value[TOTAL_SM_VALUES];
	};
	
	
	struct cached_map_list
	{
		RSP_UINT16 offset;
		RSP_UINT16 map_page;
		cached_map_list* next;
	};
#define SPECIAL_COMMAND_RANGE (1000)
#define SPECIAL_COMMAND_PAGE (14417921)	
#define MAX_SPECIAL_COMMAND_PAGE (SPECIAL_COMMAND_PAGE + SPECIAL_COMMAND_RANGE)
#define CMT_SIZE_COMMAND_PAGE (14417920)	
#define MAX_COMMAND (450)
	struct command
	{
		RSP_UINT32 dst_LPN, src_LPN;
	};
	struct special_command
	{
		RSP_UINT32 command_count;
		RSP_UINT32	remap_offset;
		bool flush_enable;
		bool command_type[MAX_COMMAND];//VC, VM
		command command_entry[MAX_COMMAND];
	};


	//static RSP_VOID RSP_ASSERT(RSP_BOOL temp);

	class ATLWrapper
	{


	public:
		

		RSP_UINT32 cur_remap_lpn;
		



#ifdef __NAND_virtual_H
		//for test
		RSP_UINT8 __COREID__;
		ATLWrapper(VFLWrapper* pVFL, RSP_UINT8 core_id);
#endif

		ATLWrapper(VFLWrapper* pVFL);
		virtual ~ATLWrapper(RSP_VOID);

		//initialize function
		static RSP_VOID ATL_initialize_manage(RSP_UINT8 L2P_size, RSP_UINT8 P2L_size, RSP_UINT8 flush_type, RSP_UINT8 init_type, RSP_BOOL BG_IC);
		static RSP_VOID initialize_handler(RSP_UINT32 SEQ_TH, RSP_UINT32 RAND_TH, RSP_UINT32 RAND_COUNT);
		//block management
		static RSP_UINT32 get_free_blk(free_blk_head* list);
		static RSP_VOID add_free_blk(free_blk_head* list, BLK_STRUCT* temp);
		static RSP_VOID del_free_blk(free_blk_head* list);

		//Map management
		static RSP_VOID unmap(RSP_UINT32 lpn, RSP_UINT32 ppn);
		static RSP_UINT32 map_incremental_garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 cache_type);
		static RSP_VOID map_read(RSP_UINT32 map_offset, RSP_UINT32 cache_offset, RSP_UINT8 type, RSP_UINT32 cache_type);
		static RSP_VOID map_write(RSP_UINT32 map_offset, RSP_UINT32 cache_offset, RSP_UINT8 type, RSP_UINT32 cache_type);
		static RSP_UINT32 get_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 cache_type);
		static RSP_VOID set_map_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount, RSP_UINT32 cache_type);

		//generic I/O
		RSP_UINT32 RSP_ReadPage(RSP_UINT32 request_ID, RSP_LPN LPN, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress);
		RSP_BOOL RSP_WritePage(RSP_LPN LPN[2], RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress);

		//I/O helper
		RSP_BOOL write_page(RSP_LPN lpn, RSP_SECTOR_BITMAP SectorBitmap, RSP_UINT32* BufferAddress, RSP_BOOL end_io);
		static RSP_BOOL meta_write_page(RSP_LPN lpn, RSP_UINT32* BufferAddress, RSP_BOOL need_mem_copy, RSP_BOOL need_unmap);
		static RSP_UINT32 assign_new_write_ppn(RSP_UINT32 channel, RSP_UINT32 bank);


		//L2P mapping table management
		static RSP_UINT32 get_ppn(RSP_UINT32 lpn, RSP_UINT8 type);
		static RSP_VOID set_ppn(RSP_UINT32 lpn, RSP_UINT32 ppn, RSP_UINT8 type);
		static RSP_VOID CMT_manage(RSP_UINT32 lpn, RSP_UINT32 *cache_slot, RSP_UINT8 type, RSP_UINT32 cache_type);

		//L2P mapping table management
		static RSP_UINT32 get_P2L(RSP_UINT32 ppn, RSP_UINT8 type);
		static RSP_VOID set_P2L(RSP_UINT32 lpn, RSP_UINT32 ppn, RSP_UINT8 type);
		

		//Vcount management
		static RSP_UINT32 get_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);
		static RSP_VOID set_vcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 vcount);
		static RSP_UINT32 get_refcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);
		static RSP_VOID set_refcount(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block, RSP_UINT32 refcount);


		//Valid bitmap management
		static RSP_VOID set_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn);
		static RSP_BOOL get_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn);
		static RSP_VOID clear_valid(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 ppn);


		//LPN_list management
		static RSP_UINT32 alloc_free_list();
		static RSP_VOID LPN_list_flush();

		//VC-VM
		static RSP_VOID Special_command_handler(RSP_UINT32 *BufferAddress);
		static RSP_VOID Virtual_copy(RSP_UINT32 dst_LPN, RSP_UINT32 src_LPN);
		static RSP_VOID Virtual_move(RSP_UINT32 dst_LPN, RSP_UINT32 src_LPN);
		static RSP_VOID remap_inter_core(RSP_UINT32 dst_LPN, RSP_UINT32 src_LPN);
		static RSP_VOID dec_valid_count(RSP_UINT32 input_lpn);
		static RSP_VOID set_realcopy(RSP_UINT32 src_lpn, RSP_UINT32 dst_lpn);
		static RSP_UINT32 alloc_vpn(RSP_UINT32 dst_ppn);
		static RSP_VOID add_list(RSP_UINT32 vpn, RSP_UINT32 lpn);
		static RSP_BOOL del_list(RSP_UINT32 vpn, RSP_UINT32 lpn);
		static RSP_VOID V2L_log_logging(RSP_UINT32 vpn, RSP_UINT32 lpn, RSP_BOOL cmd); 



		//Garbage Collection
		static RSP_VOID garbage_collection(RSP_UINT32 channel, RSP_UINT32 bank);
		static RSP_UINT8 incremental_garbage_collection(RSP_UINT32 channel,RSP_UINT32 bank, RSP_UINT8 flag);
		static RSP_UINT32 get_vt_vblock(RSP_UINT32 channel, RSP_UINT32 bank);
		static RSP_VOID GC_write_buffer(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 free_ppn, RSP_UINT8 flag);

		//Initialize
		RSP_BOOL RSP_Open(RSP_VOID);


		//helper
		static RSP_VOID RSP_BufferCopy(RSP_UINT32* pstDescBuffer, RSP_UINT32* pstSrcBuffer, RSP_UINT32 count);
		static RSP_BOOL RSP_CheckBit(RSP_SECTOR_BITMAP nVar, RSP_SECTOR_BITMAP nBit);
		static RSP_VOID* add_addr(RSP_VOID* start_addr, RSP_UINT32 offset);
		static RSP_VOID* sub_addr(RSP_VOID* start_addr, RSP_UINT32 offset);



		//flush
		static RSP_VOID flush(RSP_VOID);
		static RSP_VOID flush_Remap(RSP_VOID);
		static RSP_VOID map_flush(RSP_VOID);
		static RSP_VOID meta_flush(RSP_VOID);
		static RSP_VOID bank_meta_flush(RSP_UINT32 channel, RSP_UINT32 bank);
		static RSP_VOID flush_banks();
		static RSP_VOID V2L_flush(RSP_VOID);
		static RSP_VOID buffer_flush(RSP_UINT32 channel, RSP_UINT32 bank);
		static RSP_VOID dummy_buffer_flush();
		static RSP_VOID meta_buffer_flush();
		static RSP_VOID valid_bitmap_flush();
		static RSP_VOID bank_valid_bitmap_flush(RSP_UINT32 channel, RSP_UINT32 bank);
		
		//VC-VM: intercore_copy
		static RSP_UINT32 _FTL_ReadData2Buffer(RSP_UINT32 nLPN, RSP_UINT32 mode);
		static RSP_UINT32 FTL_ReadData2Buffer(RSP_UINT32 nLPN, RSP_UINT8* pnBuf, RSP_UINT32 nNumSectorsPerPage);
		static RSP_UINT32 FTL_WriteData2Buffer(RSP_UINT32 nLPN, RSP_UINT8* pnBuf, RSP_UINT32 nNumSectorsPerPage);
		static RSP_UINT32 FTL_Trim(RSP_UINT32 input_lpn, RSP_UINT32 count);
		static void realcopy_read();
		static void _realcopy_read(RSP_UINT32 input_ppn, RSP_UINT8 *pnBuf, RSP_SECTOR_BITMAP SectorBitmap);
		static RSP_BOOL check_realcopy_done();

		static void complete_cur_VC();
		static void read_new_VC();
		
		static RSP_BOOL one_real_copy();
		static void Check_other_core_read();
		static void cur_VC_end();

		static void FTL_Idle(void);
		static void erase_wrapper(RSP_UINT32 channel, RSP_UINT32 bank, RSP_UINT32 block);


		static RSP_VOID test_count(RSP_UINT32 count);
		static 	RSP_VOID check_ppn_is_valid(RSP_UINT32 ppn);
#ifdef Hesper_DEBUG
		//debug		
		RSP_BOOL is_in_list(RSP_UINT32 vpn, RSP_UINT32 lpn);
		RSP_VOID valid_count_test();
		RSP_VOID V2L_test();
		RSP_VOID L2P_spare_test();
		RSP_VOID L2P_test();
		static RSP_VOID test_count(RSP_UINT32 count);
		static RSP_VOID VC_struct_test(special_command vc);
		static void Check_cache_slot(RSP_UINT32* buff_addr);
#endif
	};
} //endof namespace 

#endif
