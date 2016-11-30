/*
* This file is part of the libsumo - a reverse-engineered library for
* controlling Parrot's connected toy: Jumping Sumo
*
* Copyright (C) 2014 I. Loreen
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either version 2.1 of
* the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301 USA
*/
#pragma once
#include "realtime.h"
#include "protocol.h"
#include "control.h"
#include <Windows.h>

#define BILLION                             (1E9)

static BOOL g_first_time = 1;
static LARGE_INTEGER g_counts_per_sec;

int clock_gettime(int dummy, struct timespec *ct)
{
	LARGE_INTEGER count;
	printf("clock_gettime\n");
	if (g_first_time)
	{
		g_first_time = 0;

		if (0 == QueryPerformanceFrequency(&g_counts_per_sec))
		{
			g_counts_per_sec.QuadPart = 0;
		}
	}

	if ((NULL == ct) || (g_counts_per_sec.QuadPart <= 0) ||
		(0 == QueryPerformanceCounter(&count)))
	{
		return -1;
	}

	ct->tv_sec = count.QuadPart / g_counts_per_sec.QuadPart;
	ct->tv_nsec = ((count.QuadPart % g_counts_per_sec.QuadPart) * BILLION) / g_counts_per_sec.QuadPart;

	return 0;
}

//struct timespec { long tv_sec; long tv_nsec; };    //header part
//int clock_gettime(int, struct timespec *spec)      //C-file part
//{
//	__int64 wintime; GetSystemTimeAsFileTime((FILETIME*)&wintime);
//	wintime -= 116444736000000000i64;  //1jan1601 to 1jan1970
//	spec->tv_sec = wintime / 10000000i64;           //seconds
//	spec->tv_nsec = wintime % 10000000i64 * 100;      //nano-seconds
//	return 0;
//}

namespace sumo {


	void RealTime::heartBeatIn()
	{
		while (!_stop) {

			uint8_t *b = _in.getMessage();
			if (!b)
				break;

			auto *s = reinterpret_cast<struct sync *>(b);
			printf("HEARTBEAT: %d %d %d.%09d\n", s->head.ext, s->head.seqno, s->seconds, s->nanoseconds);
			s->head.ext = 1;
			_dp->send(*s);
			delete[] b;//<- wsl ist hier der fehler
		}
	}

	void RealTime::heartBeatOut()
	{
		bool waiting_for_response = false;
		uint8_t seqno = 1;
		while (!_stop) {

			if (_sendHeartBeatOut) {
				if (waiting_for_response) {
					uint8_t *b = _out.getMessage(5);
					if (b != 0)
						waiting_for_response = false;
					delete[] b;
				}
				else {
					struct timespec tp;
					if (clock_gettime(1, &tp) != 0) {
						msleep(10);
						continue;
					}

					struct sync sync(seqno++, (uint32_t)tp.tv_sec, (uint32_t)tp.tv_nsec);
					if (!_dp->send(sync)) {
						msleep(100);
						continue;
					}
					waiting_for_response = true;
				}
			}

			if (_sendControl) {
				struct move m(seqno++, _speed != 0 || _turn != 0, _speed, _turn);
				_dp->send(m);

				msleep(20);
			}

			if (!_sendHeartBeatOut && !_sendControl)
				msleep(50);
		}
	}

};
