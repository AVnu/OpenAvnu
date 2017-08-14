/*
 * Copyright (C) 2017 Art and Logic.
 *
 * Implementation for a linux specific socket abstration providing an object 
 * based interface for dealing with lower level sockets.
 */
#include <iostream>
#include <stdexcept>

#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <cstring>
#include <cerrno>

#include "socket.hpp"

ASocket::ASocket(const std::string& interfaceName, uint16_t port, 
 int ipVersion) :
 fInterfaceName(interfaceName),
 fBindOk(false),
 fIngressTimeNano(0),
 fContinue(true),
 fIpVersion(ipVersion)
{
	fPort = port;
	int domain = 4 == ipVersion ? AF_INET : AF_INET6;
	fSocketDescriptor = socket(domain, SOCK_DGRAM, IPPROTO_UDP);
	if (-1 == fSocketDescriptor)
	{
		std::string msg = "Failed to open socket: ";
		msg += strerror(errno);
		//std::cerr << msg << std::endl;
		throw(std::runtime_error(msg));
	}
	else
	{
		struct ifreq device;
		memset(&device, 0, sizeof(device));
		interfaceName.copy(device.ifr_name, IFNAMSIZ - 1);
		
		int err = ioctl(fSocketDescriptor, SIOCGIFHWADDR, &device);
		if (-1 == err)
		{
			std::string msg = "Failed to get interface address for '" +
			 interfaceName + "': " + strerror(errno);
			//std::cerr << msg << std::endl;
			throw(std::runtime_error(msg));
		}
		else
		{
			err = ioctl(fSocketDescriptor, SIOCGIFINDEX, &device);
			if (-1 == err)
			{
				std::string msg = "Failed to get interface index: ";
				msg += strerror(errno);
				//std::cerr << msg << std::endl;
				throw(std::runtime_error(msg));
			}
			else
			{
				fInterfaceIndex = device.ifr_ifindex;

				std::cout << "createInterface  ifindex: " << fInterfaceIndex
				 << std::endl;

				Bind();
			}
		}
	}
}
ASocket::~ASocket()
{
	// Intentionally left blank
}

bool ASocket::Open(std::mutex& keeper)
{
	bool ok = false;
	while (fContinue)
	{
		// Check for messages and process them
		Process(keeper);
	}
	return ok;
}

bool ASocket::Close()
{
	fContinue = false;
	return true;
}

bool ASocket::Process(std::mutex& keeper)
{
	bool ok = true;
	ARawPacket msg;
	if (Receive(msg, keeper))
	{
		// Call the derrived classes implementation
		ProcessData(msg);
	}
	return ok;
}

bool ASocket::Receive(ARawPacket& data, std::mutex& keeper)
{
	bool haveData = false;
	struct timeval timeout = { 0, 16000 }; // 16 ms
	{
		std::lock_guard<std::mutex> guard(keeper);

		FD_ZERO(&fReadfds);
		FD_SET(fSocketDescriptor, &fReadfds);

		int err = select(fSocketDescriptor+1, &fReadfds, NULL, NULL, &timeout);
		if (0 == err) 
		{
			// Intentionally left blank
		}
		else if (-1 == err) 
		{
			std::string msg = "select ";
			msg += (EINTR == errno ? "recv signal" : "failed");
			//std::cerr << msg << std::endl;
			throw(std::runtime_error(msg));
		} 
		else if (!FD_ISSET(fSocketDescriptor, &fReadfds)) 
		{
			std::string msg = "FD_ISSET  failed";
			//std::cerr << msg << std::endl;
			throw(std::runtime_error(msg));
		}
		else
		{
			haveData = ReceiveData(data, keeper);
		}
	}
	return haveData;
}

bool ASocket::Send(std::mutex& keeper, const std::string& ipAddress, int port,
 const ARawPacket& packet)
{
	bool ok = true;

	struct sockaddr_in remoteIpv4;
	struct sockaddr_in6 remoteIpv6;
	sockaddr * remote;
	size_t remoteSize;

	if (4 == fIpVersion)
	{
		memset(&remoteIpv4, 0, sizeof(remoteIpv4));
		remoteIpv4.sin_port = htons(port);
		remoteIpv4.sin_family = AF_INET;
		remote = reinterpret_cast<sockaddr*>(&remoteIpv4);
		remoteSize = sizeof(remoteIpv4);
		inet_pton(AF_INET, ipAddress.c_str(), &remoteIpv4.sin_addr);
	}
	else if (6 == fIpVersion)
	{
		memset(&remoteIpv6, 0, sizeof(remoteIpv6));
		remoteIpv6.sin6_port = htons(port);
		remoteIpv6.sin6_family = AF_INET6;
		remote = reinterpret_cast<sockaddr*>(&remoteIpv6);
		remoteSize = sizeof(remoteIpv6);
		inet_pton(AF_INET6, ipAddress.c_str(), &remoteIpv6.sin6_addr);
	}
	else
	{
		std::string msg = "Invalid ip version " + std::to_string(fIpVersion);
		throw(std::runtime_error(msg));
	}	

	std::lock_guard<std::mutex> guard(keeper);

	const ARawPacket::Buffer& data = packet.Data();

	int err = sendto(fSocketDescriptor, &data[0], data.size(), 0, remote, remoteSize);
	if (-1 == err)
	{
		//std::cerr << "Failed to send(" << errno << "):" << strerror(errno);
		ok = false;
	}
	return ok;
}

void ASocket::IpVersion(int version)
{
	fIpVersion = version;
}

void ASocket::Bind()
{
	sockaddr_in addrIpv4;
	sockaddr_in6 addrIpv6;
	sockaddr *addr;
	size_t addrSize;
	if (4 == fIpVersion)
	{
		memset(&addrIpv4, 0, sizeof(addrIpv4));
		addrIpv4.sin_port = htons(fPort);
		addrIpv4.sin_addr.s_addr = htonl(INADDR_ANY);
		addrIpv4.sin_family = AF_INET;
		addr = reinterpret_cast<sockaddr*>(&addrIpv4);
		addrSize = sizeof(addrIpv4);
	}
	else if (6 == fIpVersion)
	{
		memset(&addrIpv6, 0, sizeof(addrIpv6));
		addrIpv6.sin6_port = htons(fPort);
		addrIpv6.sin6_addr = in6addr_any;
		addrIpv6.sin6_family = AF_INET6;
		addr = reinterpret_cast<sockaddr*>(&addrIpv6);
		addrSize = sizeof(addrIpv6);
	}
	else
	{
		std::string msg = "Invalid ip version " + std::to_string(fIpVersion);
		throw(std::runtime_error(msg));
	}
	int err = bind(fSocketDescriptor, addr, addrSize);
	if (-1 == err)
	{
		//std::cerr << "Call to bind() for failed (" << errno  << "): "
		// << strerror(errno) << std::endl;
		fBindOk = false;
	}
	else
	{
		fBindOk = true;
	}			
}

bool ASocket::ReceiveData(ARawPacket& data, std::mutex& keeper)
{
	bool haveData = false;

   struct msghdr msg;
	struct {
		struct cmsghdr cm;
		char control[256];
	} control;
	struct sockaddr_in remoteIpv4;
	struct sockaddr_in6 remoteIpv6;
	size_t remoteSize;
	sockaddr *remote;
	if (4 == fIpVersion)
	{
		memset(&remoteIpv4, 0, sizeof(remoteIpv4));
		remoteIpv4.sin_port = htons(fPort);
		remoteIpv4.sin_addr.s_addr = htonl(INADDR_ANY);
		remote = reinterpret_cast<sockaddr*>(&remoteIpv4);
		remoteSize = sizeof(remoteIpv4);
	}
	else if (6 == fIpVersion)
	{
		memset(&remoteIpv6, 0, sizeof(remoteIpv6));
		remoteIpv6.sin6_port = htons(fPort);
		remoteIpv6.sin6_addr = in6addr_any;
		remote = reinterpret_cast<sockaddr*>(&remoteIpv6);
		remoteSize = sizeof(remoteIpv6);
	}
	else
	{
		std::string msg = "Invalid ip version " + std::to_string(fIpVersion);
		throw(std::runtime_error(msg));
	}

	struct iovec sgentry;

	uint8_t buf[kReceiveBufferSize];

	memset(&msg, 0, sizeof(msg));

	msg.msg_iov = &sgentry;
	msg.msg_iovlen = 1;

	sgentry.iov_base = buf;
	sgentry.iov_len = kReceiveBufferSize;

	msg.msg_name = remote;
	msg.msg_namelen = remoteSize;
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	size_t bytesReceived = recvmsg(fSocketDescriptor, &msg, 0);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	if (bytesReceived < 0) 
	{
		// if (ENOMSG == errno)
		// {
		// 	// std::cerr << "Got ENOMSG: " << __FILE__
		// 	//  << ":" << __LINE__ << std::endl;
		// }
		// else
		// {
		// 	// std::cerr << "recvmsg() failed: " << strerror(errno)
		// 	//  << std::endl;
		// }
	}
	else
	{
		data.IngressTimeNano(ts);
		size_t toCopySize = bytesReceived < kReceiveBufferSize
		 ? bytesReceived : kReceiveBufferSize;
		data.Assign(buf, toCopySize);
		if (4 == fIpVersion)
		{
			data.RemoteAddress(remoteIpv4);	
		}
		else
		{
			data.RemoteAddress(remoteIpv6);
		}
		
		haveData = true;
	}
	return haveData;
}