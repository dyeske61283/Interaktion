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
#include "control.h"

#include <winsock2.h>
#include <winsock.h>
#include <sys/types.h>
#include <stdio.h>
//#include "_unistd_h.h"
#include <windows.h>
#include <stdlib.h>
#include <ws2tcpip.h> 
#include <iostream>
#include <cmath>

#include <time.h>

//#include "image.h"
#include "realtime.h"

#include "decode.h"

#include "protocol.h"


#pragma warning (disable:4996)

#define SCK_VERSION1            0x0101
#define SCK_VERSION2            0x0202
namespace sumo {

	class ControlIn : public MessageQueue, public StoppableThread
	{
		Control *_dp;

		std::string _date, _time;
		bool _info_done;
		uint8_t _battery;

		std::mutex              _expect_mutex;
		std::condition_variable _cv;

	public:
		ControlIn(Control *d) : MessageQueue(), _dp(d), _date(), _time(), _info_done(false), _battery(0), _expect_mutex(), _cv()
		{ }

		void control()
		{
			uint8_t seqno = 1;
			while (!_stop) {
				uint8_t *b = getMessage();
				if (!b)
					break;

				auto *io = reinterpret_cast<struct ioctl_packet *>(b);
				char *payload = reinterpret_cast<char *>(b + sizeof(*io));

				fprintf(stderr, "received in ioctl seqno: %d\n", io->head.seqno);
				fprintf(stderr, "ioctl: flags %02x, type: %d, func: %d; unk: %d\t", io->flags, io->type, io->func, io->unk);

				switch (io->type) {
				case 3:
					switch (io->func) {
					case 0: {
						std::lock_guard<std::mutex> guard(_expect_mutex);
						_info_done = true;
						fprintf(stderr, "last info\n");
						_cv.notify_all();
					} break;
					default:
						fprintf(stderr, "ioctl: unhandled ioctl func (%d) for type 0\n", io->func);
						break;
					}
					break;

				case 5: /* confirm */
					switch (io->func) {
					case 1:
						_battery = reinterpret_cast<struct ioctl<uint8_t> *>(b)->param;
						fprintf(stderr, "battery level: %d\n", _battery);
						break;
					case 2:
						fprintf(stderr, "info ??: %s\n", payload + 1);
						break;
					case 4: {
						std::lock_guard<std::mutex> guard(_expect_mutex);
						_date = payload;
						fprintf(stderr, "confirm date: %s\n", payload);
						_cv.notify_all();
					} break;
					case 5: {
						std::lock_guard<std::mutex> guard(_expect_mutex);
						_time = payload;
						fprintf(stderr, "confirm time: %s\n", payload);
						_cv.notify_all();
					} break;
					default:
						fprintf(stderr, "ioctl: unhandled ioctl func (%d) for type 5\n", io->func);
						break;
					}
					break;
				default:
					fprintf(stderr, "ioctl: unhandled ioctl func (%d/%d)\n", io->type, io->func);
					break;
				}

				struct ack ack(io->head.ext | 0x80, seqno++, io->head.seqno);
				_dp->send(ack);

				delete[] b;
			}
		}

		bool waitForDateIn()
		{
			std::unique_lock<std::mutex> lock(_expect_mutex);
			return _cv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return _date != ""; });
		}

		bool waitForTimeIn()
		{
			std::unique_lock<std::mutex> lock(_expect_mutex);
			return _cv.wait_for(lock, std::chrono::milliseconds(100), [this]() { return _time != ""; });
		}

		bool waitForInfo()
		{
			std::unique_lock<std::mutex> lock(_expect_mutex);
			return _cv.wait_for(lock, std::chrono::milliseconds(1000), [this]() { return _info_done; });
		}

		uint8_t batteryLevel() { return _battery; }
	};

	void Control::dispatch()
	{
		uint8_t buf[65535];
		while (!_stop) {

			int len = recvfrom(_udp,(char*)buf, (uint8_t)sizeof(buf),0, INADDR_ANY, 0);
			if (len <= 0)
			{
				printf("problems with recv dispatch()\n");
				break;
			}
			uint8_t *p = buf;
			udpIn(buf, len);
			while (len) {
				struct header *hdr = (struct header *) p;
				if (hdr->size > len) {
					fprintf(stderr, "not enough data in this packet, packet needs reassembly TODO\n");
					break;
				}

				switch (hdr->type) {

				case SYNC:
					if (hdr->ext == 1) /* ack */
						_rt->outMsg().sendMessage(p, hdr->size);
					else
						_rt->inMsg().sendMessage(p, hdr->size);
					break;

				//case IMAGE:
				//	if (_image)
				//		_image->sendMessage(p, hdr->size);
				//	break;

				case ACK:
					sendMessage(p, hdr->size);
					break;

				case IOCTL:
					_ctrl_in->sendMessage(p, hdr->size);
					break;

				default:
					fprintf(stderr, "unhandled in type: %d\n", hdr->type);
					break;
				}

				p += hdr->size;
				len -= hdr->size;
			}
		}
	}

	bool Control::waitForAck(const struct ioctl_packet &h)
	{
		uint8_t *b = getMessage();
		if (!b)
			return false;

		bool ret = false;
		auto *a = reinterpret_cast<struct ack *>(b);
		if (a->head.type != ACK) {
			fprintf(stderr, "bad header\n");
			goto out;
		}

		if (a->head.seqno != h.head.seqno) {
			fprintf(stderr, "did not receive acknowledge for my message: %d instead of %d\n", h.head.seqno, a->head.seqno);
			goto out;
		}

		if (a->head.ext != (h.head.ext | 0x80)) {
			fprintf(stderr, "unexpected ext-flags\n");
			goto out;
		}

		ret = true;
	out:
		delete[] b;

		return ret;
	}

	void Control::sendDate()
	{
		time_t t = ::time(0);
		struct tm malinas_tm;
		localtime_s(&malinas_tm, &t);

		struct date d(_seqno++);
		strftime(d.param, sizeof(d.param), "%Y-%m-%d", &malinas_tm);
		blockingSend(d);
	}

	void Control::sendTime()
	{
		time_t t = ::time(0);
		struct tm malinas_tm;
		localtime_s(&malinas_tm, &t);

		struct time p(_seqno++);
		strftime(p.param, sizeof(p.param), "T%H%M%S%z", &malinas_tm);
		blockingSend(p);
	}

	void Control::getInfo()
	{
		blockingSend(ioctl_packet(_seqno++, sizeof(ioctl_packet), 2, 0, 0));
	}

	void Control::enableStuff()
	{
		blockingSend(ioctl_packet(_seqno++, sizeof(ioctl_packet), 4, 0, 0));

		struct ioctl<uint8_t> i(_seqno++, 18, 0);
		i.param = 1;
		blockingSend(i);

		struct ioctl<uint8_t> o(_seqno++, 8, 0);
		o.param = 1;
		blockingSend(o);
	}

	bool Control::send(const struct packet &b)
	{
		std::lock_guard<std::mutex> __g(_send_lock);

		udpOut((uint8_t *) &b, b.head.size);

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = inet_addr("192.168.2.1");
		addr.sin_port = htons(54321);

		
		
		int ret = ::sendto(_udp, (const char*)&b, (uint8_t)b.head.size, 0, (struct sockaddr *) &addr, sizeof(addr));
		if (ret < 0) {
			perror("sendto failed");
			return false;
		}
		if (ret != b.head.size) {
			fprintf(stderr, "written data is not equal to length - packet partially sent - %lu/%d\n", ret, b.head.size);
			//	b += ret;
			//	len -= ret;
			//	goto first;
		}
		return true;
	}

	bool Control::blockingSend(const struct ioctl_packet &b)
	{
		send(b);
		return waitForAck(b);
	}

	void Control::highJump()
	{
		blockingSend(jump(_seqno++, jump::High));
	}

	void Control::longJump()
	{
		blockingSend(jump(_seqno++, jump::Long));
	}

	void Control::quickTurn(float angle)
	{
		blockingSend(turn(_seqno++, angle));
	}

	void Control::handstandBalance()
	{
		blockingSend(flip(_seqno++, flip::Balance));
	}

	void Control::flipUpsideDown()
	{
		blockingSend(flip(_seqno++, flip::UpsideDown));
	}

	void Control::flipDownsideUp()
	{
		blockingSend(flip(_seqno++, flip::DownsideUp));
	}

	void Control::swing()
	{
		blockingSend(special(_seqno++, special::Swing));
	}

	void Control::turnAndJump()
	{
		blockingSend(special(_seqno++, special::TurnAndJump));
	}

	void Control::quickTurnRight()
	{
		blockingSend(special(_seqno++, special::QuickTurnRight));
	}

	void Control::lookLeftAndRight()
	{
		blockingSend(special(_seqno++, special::LookLeftAndRight));
	}

	void Control::tap()
	{
		blockingSend(special(_seqno++, special::Tap));
	}

	void Control::quickTurnRightLeft()
	{
		blockingSend(special(_seqno++, special::QuickTurnRightLeft));
	}

	void Control::turnToBalance()
	{
		blockingSend(special(_seqno++, special::TurnToBalance));
	}

	void Control::slalom()
	{
		blockingSend(special(_seqno++, special::Slalom));
	}

	void Control::growingCircles()
	{
		blockingSend(special(_seqno++, special::GrowingCircles));
	}


	Control::~Control()
	{
		//delete _image;
	}


	bool Control::open()
	{
		WSADATA wsadata;
		int iResult;
		int sockfd;
		struct sockaddr_in servaddr;

		iResult = WSAStartup(SCK_VERSION2, &wsadata);

		if (iResult)
			return false;

		if (wsadata.wVersion != 0x0202)
		{
			WSACleanup(); //Clean up Winsock
			return false;
		}

		sockfd = socket(AF_INET, SOCK_STREAM, 0);

		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = inet_addr("192.168.2.1");
		servaddr.sin_port = htons(44444);

		if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
			fprintf(stderr, "connect failed\n");
			return false;
		}
		int serveraddrSize = sizeof(servaddr);
		int* serveraddSizePtr = &serveraddrSize;
		char buffer[1024] = "{\"controller_name\":\"PC\",\"controller_type\":\"PC\",\"d2c_port\":54321}";
		sendto(sockfd, (char*)buffer, sizeof(buffer),0,0,0);//
		int len = recvfrom(sockfd, buffer, 1024,0, (struct sockaddr *) &servaddr, serveraddSizePtr);
		buffer[len] = '\0';
		printf("config: '%s'\n", buffer);

		closesocket(sockfd);

		_udp = socket(AF_INET, SOCK_DGRAM, 0);
		if (_udp < 0) {
			fprintf(stderr, "DGRAM socket failed\n");
			return false;
		}

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(54321);
		if (bind(_udp, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
			fprintf(stderr, "bind failed\n");
			goto bind_error;
		}

		/* init object */
		_rt = new RealTime(this);
		_rt->reset();
		_rt_thread_out = std::thread(&RealTime::heartBeatIn, _rt);
		_rt_thread_in = std::thread(&RealTime::heartBeatOut, _rt);

		//if (_image) {
		//	_image->reset();
		//	_image_thread = std::thread(&Image::process, _image);
		//}
		//else {
		//	fprintf(stderr, "WARNING: no image display or processing object present. Images will be dropped\n");

		////}

		_ctrl_in = new ControlIn(this);
		_ctrl_in->reset();
		_ctrl_in_thread = std::thread(&ControlIn::control, _ctrl_in);

		reset();
		_dispatch_thread = std::thread(&Control::dispatch, this);

		/* init protocol */
		_seqno = 1;
		sendDate();
		_ctrl_in->waitForDateIn();

		sendTime();
		_ctrl_in->waitForTimeIn();

		getInfo();
		_ctrl_in->waitForInfo();

		enableStuff();

		_rt->activateControl(true);
		//	_rt->activateHeartBeatOut(true);

		return true;

	bind_error:
		closesocket(_udp);

		return false;
	}

	void Control::close()
	{
		closesocket(_udp); /* close udp before so that a blocking recv waked up */
		stop();
		_dispatch_thread.join();

		_ctrl_in->stop(); _ctrl_in->sendMessage(0, 0);
		_ctrl_in_thread.join();
		delete _ctrl_in; _ctrl_in = 0;

		_rt->stop(); _rt->inMsg().sendMessage(0, 0); _rt->outMsg().sendMessage(0, 0);
		_rt_thread_out.join();
		_rt_thread_in.join();
		delete _rt; _rt = 0;

		//if (_image) {
		//	_image->stop(); _image->sendMessage(0, 0);
		//	_image_thread.join();
		//}
		//Close the socket if it exists
		WSACleanup(); //Clean up Winsock
	}

	void Control::move(int8_t s, int8_t t)
	{
		if (_rt) {
			_rt->setSpeed(s);
			_rt->setTurn(t);
		}
	}

	int Control::batteryLevel()
	{
		return _ctrl_in->batteryLevel();
	}

}
