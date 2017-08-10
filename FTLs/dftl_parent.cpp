/* Copyright 2011 Matias Bjørling */

/* dftp_ftl.cpp  */

/* FlashSim is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version. */

/* FlashSim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. */

/* You should have received a copy of the GNU General Public License
 * along with FlashSim.  If not, see <http://www.gnu.org/licenses/>. */

/****************************************************************************/

/* Implementation of the DFTL described in the paper
 * "DFTL: A Flasg Translation Layer Employing Demand-based Selective Caching og Page-level Address Mappings"
 *
 * Global Mapping Table GMT
 * Global Translation Directory GTD (Maintained in memory)
 * Cached Mapping Table CMT (Uses LRU to pick victim)
 *
 * Dlpn/Dppn Data Logical/Physical Page Number
 * Mlpn/Mppn Translation Logical/Physical Page Number
 */


#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <queue>
#include <iostream>
#include <limits>
#include "../ssd.h"

using namespace ssd;

FtlImpl_DftlParent::MPage::MPage(long vpn)
{
	this->vpn = vpn;
	this->ppn = -1;
	this->create_ts = -1;
	this->modified_ts = -1;
	this->last_visited_time = -1;
	this->cached = false;
}

double FtlImpl_DftlParent::mpage_last_visited_time_compare(const FtlImpl_DftlParent::MPage& mpage)
{
	if (!mpage.cached)
		return std::numeric_limits<double>::max();

	return mpage.last_visited_time;
}

FtlImpl_DftlParent::FtlImpl_DftlParent(Controller &controller):
	FtlParent(controller)
{
	addressPerPage = 0;
	cmt = 0;

    // Yoohyuk Lim
    // Initialize currentDataPage per stream.
    currentDataPage = new long[MULTISTREAM_LEVEL];
    for (uint i=0; i<MULTISTREAM_LEVEL; i++)
    	currentDataPage[i] = -1;

    currentTranslationPage = -1;
    
    // Yoohyuk Lim
    block_size = (SLC_MLC_ENABLE == true) ? MLC_BLOCK_SIZE
                                          : BLOCK_SIZE;

    // Yoohyuk Lim
	// Detect required number of bits for logical address size
	addressSize = log(NUMBER_OF_ADDRESSABLE_PAGES)/log(2);

	// Find required number of bits for block size
	addressPerPage = (PAGE_SIZE/ceil(addressSize / 8.0)); // 8 bits per byte

	printf("Total required bits for representation: Address size: %i Total per page: %i \n", addressSize, addressPerPage);

	totalCMTentries = CACHE_DFTL_LIMIT * addressPerPage;
	printf("Number of elements in Cached Mapping Table (CMT): %i\n", totalCMTentries);

    //TODO Yoohyuk Lim : Consider overprovisioning area
	// Initialise block mapping table.
	uint ssdSize = NUMBER_OF_ADDRESSABLE_PAGES;

	trans_map.reserve(ssdSize);
	for (uint i=0;i<ssdSize;i++)
		trans_map.push_back(MPage(i));

    /* Yoohyuk Lim
     * Cause the addressable blocks are different from actual block number.
     * Reverse_trans_map should have the actual number of blocks. */
    ssdSize = NUMBER_OF_TOTAL_BLOCKS * block_size;
	reverse_trans_map = new long[ssdSize];
}

void FtlImpl_DftlParent::consult_GTD(long dlpn, Event &event)
{
	// Simulate that we goto translation map and read the mapping page.
	Event readEvent = Event(READ, event.get_logical_address(), 1, event.get_start_time(), event.get_streamID());
	readEvent.set_address(Address(0, PAGE));
	readEvent.set_noop(true);

	if (controller.issue(readEvent) == FAILURE) { assert(false);}
	//event.consolidate_metaevent(readEvent);
	event.incr_time_taken(readEvent.get_time_taken());
	controller.stats.numFTLRead++;
}

void FtlImpl_DftlParent::reset_MPage(FtlImpl_DftlParent::MPage &mpage)
{
	mpage.create_ts = -2;
	mpage.modified_ts = -2;
	mpage.last_visited_time = -2;
}

bool FtlImpl_DftlParent::lookup_CMT(long dlpn, Event &event)
{
	if (!trans_map[dlpn].cached)
		return false;

	event.incr_time_taken(RAM_READ_DELAY);
	controller.stats.numMemoryRead++;

	return true;
}

long FtlImpl_DftlParent::get_free_data_page(Event &event)
{
	return get_free_data_page(event, true);
}

/* Yoohyuk Lim
 * Check whether the given address is the end of block */
bool FtlImpl_DftlParent::is_block_end(uint streamID)
{
    long pageNum = currentDataPage[streamID];

    if (pageNum != -1) {
        Address address = Address(currentDataPage[streamID], BLOCK);
        uint size = controller.get_block_pointer(address)->get_size();
   
        return pageNum % block_size == size - 1;
    } else
        return false;
}

/* Yoohyuk Lim
 * Return free data page according to streamID */
long FtlImpl_DftlParent::get_free_data_page(Event &event, bool insert_events)
{
    uint streamID = event.get_streamID() % MULTISTREAM_LEVEL;
    if (currentDataPage[streamID] == -1 || (is_block_end(streamID) && insert_events))
		Block_manager::instance()->insert_events(event);

    // The value of currentDataPage[streamID] is different with above one.
	if (currentDataPage[streamID] == -1 || is_block_end(streamID)) {
		// controller.get_block_pointer(Address(currentDataPage[streamID], BLOCK))->print_status();
		currentDataPage[streamID] = Block_manager::instance()->get_free_block(DATA, event).get_linear_address();
	} else
		currentDataPage[streamID]++;

	return currentDataPage[streamID];
}

FtlImpl_DftlParent::~FtlImpl_DftlParent(void)
{
	delete[] reverse_trans_map;
    delete[] currentDataPage; //Yoohyuk Lim
}

void FtlImpl_DftlParent::resolve_mapping(Event &event, bool isWrite)
{
	uint dlpn = event.get_logical_address();
	/* 1. Lookup in CMT if the mapping exist
	 * 2. If, then serve
	 * 3. If not, then goto GDT, lookup page
	 * 4. If CMT full, evict a page
	 * 5. Add mapping to CMT
	 */
	//printf("%i\n", cmt);
	if (lookup_CMT(event.get_logical_address(), event))
	{
		controller.stats.numCacheHits++;

		MPage current = trans_map[dlpn];
		if (isWrite)
		{
			current.modified_ts = event.get_start_time();			
		}
		current.last_visited_time = event.get_start_time();
		trans_map.replace(trans_map.begin()+dlpn, current);

		// evict_page_from_cache(event);    // no need to evict page from cache
	} else {
		controller.stats.numCacheFaults++;

		evict_page_from_cache(event);

		consult_GTD(dlpn, event);

		MPage current = trans_map[dlpn];
		current.modified_ts = event.get_start_time();
		current.last_visited_time = event.get_start_time();
		if (isWrite)
			current.modified_ts++;
		current.create_ts = event.get_start_time();
		current.cached = true;
		trans_map.replace(trans_map.begin()+dlpn, current);

		cmt++;
	}
}

void FtlImpl_DftlParent::evict_page_from_cache(Event &event)
{
	while (cmt >= totalCMTentries)
	{
		// Find page to evict
		MpageByLastVisited::iterator evictit = boost::multi_index::get<1>(trans_map).begin();
		MPage evictPage = *evictit;

		assert(evictPage.cached && evictPage.create_ts >= 0 && evictPage.modified_ts >= 0);

		if (evictPage.create_ts != evictPage.modified_ts)
		{
			// Evict page
			// Inform the ssd model that it should invalidate the previous page.
			// Calculate the start address of the translation page.
			int vpnBase = evictPage.vpn - evictPage.vpn % addressPerPage;

			for (int i=0;i<addressPerPage;i++)
			{
				MPage cur = trans_map[vpnBase+i];
				if (cur.cached)
				{
					cur.create_ts = cur.modified_ts;
					trans_map.replace(trans_map.begin()+vpnBase+i, cur);
				}
			}

			// Simulate the write to translate page
			Event write_event = Event(WRITE, event.get_logical_address(), 1, event.get_start_time(), event.get_streamID());
			write_event.set_address(Address(0, PAGE));
			write_event.set_noop(true);

			if (controller.issue(write_event) == FAILURE) {	assert(false);}

			event.incr_time_taken(write_event.get_time_taken());
			controller.stats.numFTLWrite++;
			controller.stats.numCellWrite[MLC]++;
			controller.stats.numGCWrite++;
		}

		// Remove page from cache.
		cmt--;

		evictPage.cached = false;
		reset_MPage(evictPage);
		trans_map.replace(trans_map.begin()+evictPage.vpn, evictPage);
	}
}

void FtlImpl_DftlParent::evict_specific_page_from_cache(Event &event, long lba)
{
		// Find page to evict
		MPage evictPage = trans_map[lba];

		if (!evictPage.cached)
			return;

		assert(evictPage.cached && evictPage.create_ts >= 0 && evictPage.modified_ts >= 0);

		if (evictPage.create_ts != evictPage.modified_ts)
		{
			// Evict page
			// Inform the ssd model that it should invalidate the previous page.
			// Calculate the start address of the translation page.
			int vpnBase = evictPage.vpn - evictPage.vpn % addressPerPage;

			for (int i=0;i<addressPerPage;i++)
			{
				MPage cur = trans_map[vpnBase+i];
				if (cur.cached)
				{
					cur.create_ts = cur.modified_ts;
					trans_map.replace(trans_map.begin()+vpnBase+i, cur);
				}
			}

			// Simulate the write to translate page
			Event write_event = Event(WRITE, event.get_logical_address(), 1, event.get_start_time(), event.get_streamID());
			write_event.set_address(Address(0, PAGE));
			write_event.set_noop(true);

			if (controller.issue(write_event) == FAILURE) {	assert(false);}

			event.incr_time_taken(write_event.get_time_taken());
			controller.stats.numFTLWrite++;
			controller.stats.numCellWrite[MLC]++;
			controller.stats.numGCWrite++;
		}

		// Remove page from cache.
		cmt--;

		evictPage.cached = false;
		reset_MPage(evictPage);
		trans_map.replace(trans_map.begin()+evictPage.vpn, evictPage);

}

void FtlImpl_DftlParent::update_translation_map(FtlImpl_DftlParent::MPage &mpage, long ppn)
{
	mpage.ppn = ppn;
	reverse_trans_map[ppn] = mpage.vpn;
}
