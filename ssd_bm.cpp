/* Copyright 2011 Matias Bjørling */

/* Block Management
 *
 * This class handle allocation of block pools for the FTL
 * algorithms.
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <queue>
#include "ssd.h"

using namespace ssd;


Block_manager::Block_manager(FtlParent *ftl) : ftl(ftl)
{
	/*
	 * Configuration of blocks.
	 * User-space is the number of blocks minus the
	 * requirements for map directory.
	 */

    // Yoohyuk Lim
    block_size = (SLC_MLC_ENABLE == true) ? MLC_BLOCK_SIZE
                                          : BLOCK_SIZE;

	max_blocks = NUMBER_OF_ADDRESSABLE_BLOCKS;
	max_log_blocks = max_blocks;
	op_size = NUMBER_OF_OVERPROVISIONING_BLOCKS; // Yoohyuk Lim

	if (FTL_IMPLEMENTATION == IMPL_FAST)
		max_log_blocks = FAST_LOG_PAGE_LIMIT;

	// Block-based map lookup simulation
    // Yoohyuk Lim: Unused variable
	max_map_pages = MAP_DIRECTORY_SIZE * block_size; 

	directoryCurrentPage = 0;
	num_insert_events = 0;

    // Yoohyuk Lim
    simpleCurrentFree = 0;

    // Yoohyuk Lim
    for (int i=0; i<CELL_TYPE_NUM; i++)
    	data_active[i] = 0;
	log_active = 0;

	current_writing_block = -2;

	out_of_blocks = false;

	active_cost.reserve(NUMBER_OF_TOTAL_BLOCKS);
}

Block_manager::~Block_manager(void)
{
	return;
}

void Block_manager::cost_insert(Block *b)
{
	active_cost.push_back(b);
}

void Block_manager::instance_initialize(FtlParent *ftl)
{
	Block_manager::inst = new Block_manager(ftl);
}

Block_manager *Block_manager::instance()
{
	return Block_manager::inst;
}

/*
 * Retrieves a page using either simple approach (when not all
 * pages have been written or the complex that retrieves
 * it from a free page list.
 */
/* Yoohyuk Lim
 * Overprovisioning logic is needed.
 * According to the number of SLC blocks,
 * decide to return a MLC or SLC block for parity block. */
void Block_manager::get_page_block(Address &address, Event &event)
{
	// We need separate queues for each plane? communication channel? communication channel is at the per die level at the moment. i.e. each LUN is a die.
    block_cell_type ctype;

    if (SLC_MLC_ENABLE == true)
        ctype = event.get_streamID() == STREAMID_PARITY ? SLC : MLC;
    else
        ctype = MLC;

	if (simpleCurrentFree < max_blocks * block_size)
	{
		address.set_linear_address(simpleCurrentFree, BLOCK);
		current_writing_block = simpleCurrentFree;
    	simpleCurrentFree += block_size;
	}
	else
	{
		if (free_list.size() <= 1 && !out_of_blocks)
		{
			out_of_blocks = true;
			insert_events(event);
		}

		assert(free_list.size() != 0);
		address.set_linear_address(free_list.front()->get_physical_address(), BLOCK);
		current_writing_block = free_list.front()->get_physical_address();
		free_list.erase(free_list.begin());
		out_of_blocks = false;
	}
   
    /* Yoohyuk Lim
     * If parity, set block to slc.
     * But if the overprovisioning area is not enough,
     * then set MLC as parity block. */
    if (!out_of_blocks && SLC_MLC_ENABLE == true)
    {
        Block *block = ftl->controller.get_block_pointer(address);
       
        if (ctype == SLC && data_active[ctype] < (uint) floor(2 * SLC_RATIO * (double)NUMBER_OF_OVERPROVISIONING_BLOCKS))
		{
			if (data_active[ctype] >= NUMBER_OF_OVERPROVISIONING_BLOCKS)
				--op_size;
            block->set_cell_type(SLC);
		}
		else
		{
            block->set_cell_type(MLC);
			ctype = MLC;
		}
    }
	
	ftl->controller.stats.numCellAlloc[ctype]++;
}


Address Block_manager::get_free_block(Event &event)
{
	return get_free_block(DATA, event);
}

/* Yoohyuk Lim : Not used in DFTL. We don't care about this.
 * Handles block manager statistics when changing a
 * block to a data block from a log block or vice versa.
 */
void Block_manager::promote_block(block_type to_type)
{
	if (to_type == DATA)
	{
		data_active[MLC]++;
		log_active--;
	}
	else if (to_type == LOG)
	{
		log_active++;
		data_active[MLC]--;
	}
}

/*
 * Returns true if there are no space left for additional log pages.
 */
bool Block_manager::is_log_full()
{
	return log_active == max_log_blocks;
}

// Yoohyuk Lim
void Block_manager::print_statistics(FILE *stream)
{
	if (stream == NULL)
		stream = stdout;

	fprintf(stream, "-----------------\n");
	fprintf(stream, "Block Statistics:\n");
	fprintf(stream, "-----------------\n");
	fprintf(stream, "Log blocks:  %lu\n", log_active);
    if (SLC_MLC_ENABLE == true)
    {
       	fprintf(stream, "Data blocks: SLC: %lu MLC: %lu\n", data_active[SLC], data_active[MLC]);
       	fprintf(stream, "Free blocks: %lu\n",
                (max_blocks - (simpleCurrentFree/block_size) + free_list.size()));
       	fprintf(stream, "Invalid blocks: %lu\n", invalid_list.size());
       	fprintf(stream, "Free2 blocks: %lu\n",
                (unsigned long int)invalid_list.size()
                + (unsigned long int)log_active
                + (unsigned long int)data_active[SLC]
                + (unsigned long int)data_active[MLC]
                - (unsigned long int)free_list.size());
    }
    else
    {
       	fprintf(stream, "Data blocks: %lu\n", data_active[MLC]);
       	fprintf(stream, "Free blocks: %lu\n", (max_blocks - (simpleCurrentFree/BLOCK_SIZE)) + free_list.size());
       	fprintf(stream, "Invalid blocks: %lu\n", invalid_list.size());
       	fprintf(stream, "Free2 blocks: %lu\n", (unsigned long int)invalid_list.size() + (unsigned long int)log_active + (unsigned long int)data_active[MLC] - (unsigned long int)free_list.size());
    }

	fprintf(stream, "-----------------\n");
}

void Block_manager::print_statistics()
{
	print_statistics(stdout);
}


/* Yoohyuk Lim : Not used in DFTL. We don't care about this. */
void Block_manager::invalidate(Address address, block_type type)
{
	invalid_list.push_back(ftl->get_block_pointer(address));

	switch (type)
	{
	case DATA:
		data_active[MLC]--;
		break;
	case LOG:
		log_active--;
		break;
	case LOG_SEQ:
		break;
	}
}

/* Yoohyuk Lim */
/* Insert erase events into the event stream.
 * The strategy is to clean up all invalid pages instantly.
 */
void Block_manager::insert_events(Event &event)
{
	// Calculate if GC should be activated.
	float used;
	float total = NUMBER_OF_TOTAL_BLOCKS;// - op_size;
	float ratio;

    if (SLC_MLC_ENABLE == true)
        // invalid_list and log_active are not used in DFTL,
        // thus we don't care about it.
        used = (int)data_active[MLC] + (int)data_active[SLC];
    else
	    used = (int)invalid_list.size() + (int)log_active + (int)data_active[MLC];

    ratio = (float) used / total;

	if (ratio < 0.9) // Magic number was (ratio < 0.9)
		return;

	uint num_to_erase = 5; // More Magic!

	double time_taken = event.get_time_taken();

//	printf("%f %4lu %4lu %4lu\n", ratio, (ulong) free_list.size(), data_active[MLC], data_active[SLC]);

    // Yoohyuk Lim : This part is not used in DFTL,
    //               because invalid_list is always zero.
	// First step and least expensive is to go though invalid list. (Only used by FAST)
	while (num_to_erase != 0 && invalid_list.size() != 0)
	{
		Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time());
		erase_event.set_address(Address(invalid_list.back()->get_physical_address(), BLOCK));
		block_cell_type ctype = ftl->controller.get_block_pointer(erase_event.get_address())->get_cell_type();

		if (ftl->controller.issue(erase_event) == FAILURE) {	assert(false);}
		event.incr_time_taken(erase_event.get_time_taken());

		free_list.push_back(invalid_list.back());
		invalid_list.pop_back();

		num_to_erase--;
		ftl->controller.stats.numFTLErase++;
		ftl->controller.stats.numFTLWL += (SLC_MLC_ENABLE == true && ctype == MLC) ? MLC_ERASE_OVERHEAD : 1;
		ftl->controller.stats.numCellErase[ctype]++;
	}

	num_insert_events++;

	if (FTL_IMPLEMENTATION == IMPL_DFTL || FTL_IMPLEMENTATION == IMPL_BIMODAL)
	{

		ActiveByCost::iterator it = active_cost.get<1>().end();
		--it;

		while (num_to_erase != 0 && (*it)->get_pages_invalid() > 0 && (*it)->get_pages_valid() == (*it)->get_size())
		{
			// Erase SLC blocks for first.
			if (SLC_MLC_ENABLE == true && (*it)->get_cell_type() != SLC)
			{
				ActiveByCost::iterator _it = it;
				
				while(_it != active_cost.get<1>().begin() && (*_it)->get_cell_type() != SLC) --_it;

				// Only if the overhead of erasing the MLC block (it) is higher than the SLC block (_it)
				if (_it != active_cost.get<1>().begin()
						&& (*_it)->get_pages_invalid() > 0
						&& (*_it)->get_pages_valid() == (*_it)->get_size()
						&& ((*_it)->get_size() - (*_it)->get_pages_invalid()) <= ((*it)->get_size() - (*it)->get_pages_invalid()))
//						&& ((*_it)->get_size() - (*_it)->get_pages_invalid()) * (SLC_WRITE_DELAY + SLC_READ_DELAY)
//							<= ((*it)->get_size() - (*it)->get_pages_invalid()) * (MLC_WRITE_DELAY + MLC_READ_DELAY))
					it = _it;
			}

			if (current_writing_block != (*it)->physical_address)
			{
				//printf("erase p: %p phy: %li ratio: %i num: %i\n", (*it), (*it)->physical_address, (*it)->get_pages_invalid(), num_to_erase);
				Block *blockErase = (*it);
				block_cell_type ctype = blockErase->get_cell_type();

				// Let the FTL handle cleanup of the block.
				ftl->cleanup_block(event, blockErase);

				// Create erase event and attach to current event queue.
				Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time(), event.get_streamID());
				erase_event.set_address(Address(blockErase->get_physical_address(), BLOCK));

				// Execute erase
				if (ftl->controller.issue(erase_event) == FAILURE) { assert(false);	}

				free_list.push_back(blockErase);
				data_active[ctype]--;
				
				if (ctype == SLC && data_active[ctype] >= NUMBER_OF_OVERPROVISIONING_BLOCKS)
					++op_size;

				event.incr_time_taken(erase_event.get_time_taken());

				ftl->controller.stats.numFTLErase++;
				ftl->controller.stats.numFTLWL += (SLC_MLC_ENABLE == true && ctype == MLC) ? MLC_ERASE_OVERHEAD : 1;
				ftl->controller.stats.numCellErase[ctype]++;
			}

			it = active_cost.get<1>().end();
			--it;
            
			if (current_writing_block == (*it)->physical_address)
                --it;

			num_to_erase--;
		}
	}

	ftl->controller.stats.GCElapsedTime += event.get_time_taken() - time_taken;
}

// Yoohyuk Lim
Address Block_manager::get_free_block(block_type type, Event &event)
{
	Address address;
	get_page_block(address, event);
    Block *block = ftl->controller.get_block_pointer(address);
    block_cell_type ctype = block->get_cell_type();
	switch (type)
	{
	case DATA:
        // Parity block can be a MLC block.
        // Adjust the current block status according its cell type.
		block->set_block_type(DATA);
        data_active[ctype]++;
		break;
	case LOG:
		if (log_active > max_log_blocks)
			throw std::bad_alloc();

		block->set_block_type(LOG);
		log_active++;
		break;
	default:
		break;
	}

	return address;
}

void Block_manager::print_cost_status(FILE *stream)
{

//	ActiveByCost::iterator it = active_cost.get<1>().begin();

//	printf("start:::\n");
//
//	for (uint i=0;i<10;i++) //SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE
//	{
//		printf("%li %i %i\n", (*it)->physical_address, (*it)->get_pages_valid(), (*it)->get_pages_invalid());
//		++it;
//	}

	printf("end:::\n");

	if (stream == NULL)
		stream = stdout;

	ActiveByCost::iterator it = active_cost.get<1>().end();
	--it;

//	fprintf(stream,"Address\tvalid\tinvalid\n");
	for (;it != active_cost.get<1>().begin();--it) //SSD_SIZE*PACKAGE_SIZE*DIE_SIZE*PLANE_SIZE
	{
		fprintf(stream,"%s\t%li\t%i\t%i\n", (*it)->get_cell_type() == SLC ? "SLC" : "MLC",
				(*it)->physical_address, (*it)->get_pages_valid(), (*it)->get_pages_invalid());
	}
}

// Yoohyuk Lim
void Block_manager::erase_and_invalidate(Event &event, Address &address, block_type btype)
{
	Event erase_event = Event(ERASE, event.get_logical_address(), 1, event.get_start_time()+event.get_time_taken(), event.get_streamID());
	erase_event.set_address(address);
   
    Block *block = ftl->controller.get_block_pointer(address);
    block_cell_type ctype = block->get_cell_type();

	if (ftl->controller.issue(erase_event) == FAILURE) { assert(false);}

	free_list.push_back(block);

	switch (btype)
	{
	case DATA:
		data_active[ctype]--;
		break;
	case LOG:
		log_active--;
		break;
	case LOG_SEQ:
		break;
	}

	event.incr_time_taken(erase_event.get_time_taken());
	ftl->controller.stats.numFTLWL += (SLC_MLC_ENABLE == true && ctype == MLC) ? MLC_ERASE_OVERHEAD : 1;
	ftl->controller.stats.numCellErase[ctype]++;
	ftl->controller.stats.numFTLErase++;
}

/* Yoohyuk Lim : Not used in DFTL. We don't care about this. */
int Block_manager::get_num_free_blocks()
{
	
    if (simpleCurrentFree < max_blocks*block_size)
		return (simpleCurrentFree / block_size) + free_list.size();
	else
		return free_list.size();
}

/* Yoohyuk Lim */
void Block_manager::update_block(Block * b)
{
    std::size_t pos = b->physical_address / block_size;
	active_cost.replace(active_cost.begin()+pos, b);
}

/* Yoohyuk Lim */
void Block_manager::add_to_free_list(Block *b)
{
	free_list.push_back(b);
}
