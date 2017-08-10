/* Copyright 2009, 2010 Brendan Tauras */

/* run_test.cpp is part of FlashSim. */

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

/* Basic test driver
 * Brendan Tauras 2009-11-02
 *
 * driver to create and run a very basic test of writes then reads */

#include "ssd.h"

#define SIZE 130

using namespace ssd;

int main()
{
	load_config();
	print_config(NULL);
	printf("\n");

	Ssd *ssd = new Ssd();

	double result;

	for (int i = 0; i < SIZE-6; i++)
	{
		/* event_arrive(event_type, logical_address, size, start_time, buffer, streamID) */
		result = ssd -> event_arrive(WRITE, 6+i, 1, (double) 1800+(300*i), &i);
		printf("Write time: %.20lf\tWrote: %d\n", result, i);
		result = ssd -> event_arrive(READ, 6+i, 1, (double) 1800+(300*i));
		printf("Read time : %.20lf\tRead : %d\n", result, *(int*) global_buffer);
	}

	delete ssd;
	return 0;
}
