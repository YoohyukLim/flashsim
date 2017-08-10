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

/* Runtime information for the SSD Model
 */

#include <new>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <iostream>
#include "ssd.h"

using namespace ssd;

Stats::Stats()
{
	reset();
}

void Stats::reset()
{
	// FTL
	numFTLRead = 0;
	numFTLWrite = 0;
	numFTLErase = 0;
	numFTLWL = 0; // Yoohyuk Lim
	numFTLTrim = 0;

	numCellAlloc[MLC] = 0;
	numCellAlloc[SLC] = 0;
	numCellWrite[MLC] = 0;
	numCellWrite[SLC] = 0;
	numCellErase[MLC] = 0;
	numCellErase[SLC] = 0;

	GCElapsedTime = 0; // Yoohyuk Lim

	//GC
	numGCRead = 0;
	numGCWrite = 0;
	numGCErase = 0;

	// WL
	numWLRead = 0;
	numWLWrite = 0;
	numWLErase = 0;

	// Log based FTL's
	numLogMergeSwitch = 0;
	numLogMergePartial = 0;
	numLogMergeFull = 0;

	// Page based FTL's
	numPageBlockToPageConversion = 0;

	// Cache based FTL's numCacheHits = 0; numCacheFaults = 0; // Memory consumptions (Bytes) numMemoryTranslation = 0; numMemoryCache = 0;

	numMemoryRead = 0;
	numMemoryWrite = 0;
}

void Stats::reset_statistics()
{
	reset();
}

void Stats::write_header(FILE *stream)
{
	fprintf(stream, "numFTLRead;numFTLWrite;numFTLErase;numFTLTrim;numGCRead;numGCWrite;numGCErase;numWLRead;numWLWrite;numWLErase;numLogMergeSwitch;numLogMergePartial;numLogMergeFull;numPageBlockToPageConversion;numCacheHits;numCacheFaults;numMemoryTranslation;numMemoryCache;numMemoryRead;numMemoryWrite\n");
}

void Stats::write_statistics(FILE *stream)
{
	fprintf(stream, "%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;%li;\n",
			numFTLRead, numFTLWrite, numFTLErase, numFTLTrim,
			numGCRead, numGCWrite, numGCErase,
			numWLRead, numWLWrite, numWLErase,
			numLogMergeSwitch, numLogMergePartial, numLogMergeFull,
			numPageBlockToPageConversion,
			numCacheHits, numCacheFaults,
			numMemoryTranslation,
			numMemoryCache,
			numMemoryRead,numMemoryWrite);

	//print_statistics();
}

void Stats::write_custom_stat(FILE *stream)
{
//	fprintf(stream, "numFTLRead numFTLWrite numFTLErase numWLRead numWLWrite numFTLWL GCElapsedTime numMLCAlloc numSLCAlloc numMLCWrite numSLCWrite numMLCErase numSLCErase numWLErase\n");
	fprintf(stream, "%li %li %li %li %li %li %f %li %li %li %li %li %li %li\n",
			numFTLRead, numFTLWrite, numFTLErase,
			numWLRead, numWLWrite,
			numFTLWL, GCElapsedTime,
			numCellAlloc[MLC], numCellAlloc[SLC],
			numCellWrite[MLC], numCellWrite[SLC],
			numCellErase[MLC], numCellErase[SLC],
			numWLErase);
}

void Stats::print_statistics()
{
	printf("Statistics:\n");
	printf("-----------\n");
	printf("FTL Reads: %li\t Writes: %li\t Erases: %li\t Trims: %li\n", numFTLRead, numFTLWrite, numFTLErase, numFTLTrim);
	printf("GC  Reads: %li\t Writes: %li\t Erases: %li\n", numGCRead, numGCWrite, numGCErase);
	printf("WL  Reads: %li\t Writes: %li\t Erases: %li\n", numWLRead, numWLWrite, numWLErase);
	printf("Log FTL Switch: %li Partial: %li Full: %li\n", numLogMergeSwitch, numLogMergePartial, numLogMergeFull);
	printf("Page FTL Convertions: %li\n", numPageBlockToPageConversion);
	printf("Cache Hits: %li Faults: %li Hit Ratio: %f\n", numCacheHits, numCacheFaults, (double)numCacheHits/(double)(numCacheHits+numCacheFaults));
	printf("Memory Consumption:\n");
	printf("Tranlation: %li Cache: %li\n", numMemoryTranslation, numMemoryCache);
	printf("Reads: %li \t Writes: %li\n", numMemoryRead, numMemoryWrite);
	printf("Overheads:\n\tErase: MLC: %li\t SLC: %li\t GC Elapsed: %f\n", numCellErase[MLC], numCellErase[SLC], GCElapsedTime); // Yoohyuk Lim
	printf("\tWrite: MLC: %li\t SLC: %li\t\n", numCellWrite[MLC], numCellWrite[SLC]); // Yoohyuk Lim
	printf("-----------\n");
}
