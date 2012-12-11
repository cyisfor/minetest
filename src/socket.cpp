/*
Minetest-c55
Copyright (C) 2010 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "socket.h"
#include "jmutexautolock.h"
#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	// Without this some of the network functions are not found on mingw
	#ifndef _WIN32_WINNT
		#define _WIN32_WINNT 0x0501
	#endif
	#include <windows.h>
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#ifdef _MSC_VER
		#pragma comment(lib, "ws2_32.lib")
	#endif
typedef SOCKET socket_t;
typedef int socklen_t;
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <arpa/inet.h>
typedef int socket_t;
#endif

#include "constants.h"
#include "debug.h"
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <errno.h>
#include "util/string.h"
#include "util/numeric.h"

bool socket_enable_debug_output = false;
#define DP socket_enable_debug_output
// This is prepended to everything printed here
#define DPS ""

bool g_sockets_initialized = false;

void sockets_init()
{
#ifdef _WIN32
	WSADATA WsaData;
	if(WSAStartup( MAKEWORD(2,2), &WsaData ) != NO_ERROR)
		throw SocketException("WSAStartup failed");
#else
#endif
	g_sockets_initialized = true;
}

void sockets_cleanup()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

Address::Address() : m_address(NULL)
{
	assert(m_address==NULL);
}

Address::~Address() {
    JMutexAutoLock context(m_lock);
//    if(m_address != NULL) freeaddrinfo(m_address);
    m_address = NULL;
}

#define ResolveAssert(what,message) { int e = what; if(e != 0) throw ResolveError(message); }

Address::Address(const char* node, const char* service) : m_address(NULL)
{
    JMutexAutoLock context(m_lock);
    assert(m_address==NULL);
    ResolveAssert(getaddrinfo(node,service,NULL,&m_address),node);
}

Address::Address(unsigned int a, unsigned int b,
		unsigned int c, unsigned int d,
		unsigned short port) : m_address(NULL)
{
    assert(m_address==NULL);
    setAddress(a,b,c,d,port);
}

bool Address::operator==(Address &address)
{
	return (serializeString() == address.serializeString());
}

bool Address::operator!=(Address &address)
{
	return !(*this == address);
}


std::string Address::serializeString() const
{
    assert(m_address);
    char node[0x100];
    char service[0x10];
    int ret = getnameinfo(m_address->ai_addr,m_address->ai_addrlen,
                          node,0x100,
                          service,0x10,
                          NI_NUMERICHOST | NI_NUMERICSERV );
    if(ret!=0) throw ResolveError("Could not get the (readable) name of an address! plz debug");
    return std::string(node)+","+service;
}

void Address::setAddress(const char* node, const char* service)
{
    JMutexAutoLock context(m_lock);
    assert(m_address == NULL);
    ResolveAssert(getaddrinfo(node,service,NULL,&m_address),node);
}

void Address::setAddress(unsigned int a, unsigned int b,
                         unsigned int c, unsigned int d, 
			 unsigned short port)
{

    JMutexAutoLock context(m_lock);
    assert(m_address==NULL);
    char node[0x100];
    char service[0x10];
    struct sockaddr_in fakeaddr;
    memset(&fakeaddr,0,sizeof(fakeaddr));
    fakeaddr.sin_family = AF_INET;
    fakeaddr.sin_addr.s_addr = htonl((a<<24) | (b<<16) | ( c<<8) | d);
    fakeaddr.sin_port = htons(port);
    assert(0==getnameinfo((struct sockaddr*)&fakeaddr,sizeof(fakeaddr),
                          node,0x100,
                          service,0x10,
               NI_NUMERICHOST | NI_NUMERICSERV));
    struct addrinfo hints = {};
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_DGRAM;
    ResolveAssert(getaddrinfo(node,service,&hints,&m_address),node);
    assert(m_address);
}

void Address::print(std::ostream *s) const
{
	(*s)<<serializeString();
}

void Address::print() const
{
	print(&dstream);
}

UDPSocket::UDPSocket(): m_handle(-1) {}
void UDPSocket::Bind(const char* service)
{
    std::cerr << "Bind " << service << std::endl;
    if(g_sockets_initialized == false)
		throw SocketException("Sockets not initialized");

    struct addrinfo *address = NULL;
    struct addrinfo hints = {};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_V4MAPPED;

    ResolveAssert(getaddrinfo(NULL,service,&hints,&address),"ready to listen");

    m_handle = socket(address->ai_family,address->ai_socktype,address->ai_protocol);

	if(DP)
	dstream<<DPS<<"UDPSocket("<<(int)m_handle<<")::UDPSocket()"<<std::endl;

    if(m_handle <= 0)
    {
		throw SocketException("Failed to create socket");
    }

    struct addrinfo* cur = address;
    while(cur) {
        if(bind(m_handle, cur->ai_addr, cur->ai_addrlen) < 0)
        {
    #ifndef DISABLE_ERRNO
    		dstream<<(int)m_handle<<": Bind failed: "<<strerror(errno)<<cur->ai_protocol<<std::endl;
    #endif
    		throw SocketException("Failed to bind socket");
        }
	cur = cur->ai_next;
	break;
    }
/*#ifdef _WIN32
	DWORD nonblocking = 0;
	if(ioctlsocket(m_handle, FIONBIO, &nonblocking) != 0)
	{
		throw SocketException("Failed set non-blocking mode");
	}
#else
	int nonblocking = 0;
	if(fcntl(m_handle, F_SETFL, O_NONBLOCK, nonblocking) == -1)
	{
		throw SocketException("Failed set non-blocking mode");
	}
#endif*/


    setTimeoutMs(0);
    freeaddrinfo(address);
    	std::cerr << "BEEP" << std::endl;
}

void UDPSocket::Close() {
   if(m_handle < 0) return;
   	if(DP)
	dstream<<DPS<<"UDPSocket("<<(int)m_handle<<")::~UDPSocket()"<<std::endl;

#ifdef _WIN32
	closesocket(m_handle);
#else
	close(m_handle);
#endif
   m_handle = -1;
}


UDPSocket::~UDPSocket()
{
	Close();
}

void UDPSocket::Send(const Address & destination, const void * data, int size)
{
	bool dumping_packet = false;
	if(INTERNET_SIMULATOR)
		dumping_packet = (myrand()%10==0); //easy
		//dumping_packet = (myrand()%4==0); // hard

	if(DP){
		/*dstream<<DPS<<"UDPSocket("<<(int)m_handle
				<<")::Send(): destination=";*/
		dstream<<DPS;
		dstream<<(int)m_handle<<" -> ";
		destination.print();
		dstream<<", size="<<size<<", data=";
		for(int i=0; i<size && i<20; i++){
			if(i%2==0) DEBUGPRINT(" ");
			unsigned int a = ((const unsigned char*)data)[i];
			DEBUGPRINT("%.2X", a);
		}
		if(size>20)
			dstream<<"...";
		if(dumping_packet)
			dstream<<" (DUMPED BY INTERNET_SIMULATOR)";
		dstream<<std::endl;
	}
	else if(dumping_packet)
	{
		// Lol let's forget it
		dstream<<"UDPSocket::Send(): "
				"INTERNET_SIMULATOR: dumping packet."
				<<std::endl;
	}

	if(dumping_packet)
		return;

	int sent = sendto(m_handle, (const char*)data, size,
		0, destination.getAddress(), destination.getLength());

    if(sent != size)
    {
		throw SendFailedException("Failed to send packet");
    }
}

int UDPSocket::Receive(Address & sender, void * data, int size)
{
	if(WaitData(m_timeout_ms) == false)
	{
		return -1;
	}

	struct sockaddr_storage address;
	memset(&address,0,sizeof(address));
	socklen_t address_len = sizeof(address);

	int received = recvfrom(m_handle, (char*)data,
			size, 0, (sockaddr*)&address, &address_len);

	if(received < 0)
		return -1;

	char node[0x100];
	char service[0x10];
	assert(0==getnameinfo((struct sockaddr*)&address,address_len,
			node,0x100,
			service,0x10,
			NI_NUMERICHOST|NI_NUMERICSERV));

	sender.setCPlusPlusSucks(Address(node,service));

	if(DP){
		//dstream<<DPS<<"UDPSocket("<<(int)m_handle<<")::Receive(): sender=";
		dstream<<DPS<<(int)m_handle<<" <- ";
		sender.print();
		//dstream<<", received="<<received<<std::endl;
		dstream<<", size="<<received<<", data=";
		for(int i=0; i<received && i<20; i++){
			if(i%2==0) DEBUGPRINT(" ");
			unsigned int a = ((const unsigned char*)data)[i];
			DEBUGPRINT("%.2X", a);
		}
		if(received>20)
			dstream<<"...";
		dstream<<std::endl;
	}

	return received;
}

int UDPSocket::GetHandle()
{
	return m_handle;
}

void UDPSocket::setTimeoutMs(int timeout_ms)
{
	m_timeout_ms = timeout_ms;
}

bool UDPSocket::WaitData(int timeout_ms)
{
	fd_set readset;
	int result;

	// Initialize the set
	FD_ZERO(&readset);
	FD_SET(m_handle, &readset);

	// Initialize time out struct
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = timeout_ms * 1000;
	// select()
	result = select(m_handle+1, &readset, NULL, NULL, &tv);

	if(result == 0){
		// Timeout
		/*dstream<<"Select timed out (timeout_ms="
				<<timeout_ms<<")"<<std::endl;*/
		return false;
	}
	else if(result < 0 && errno == EINTR){
		return false;
	}
	else if(result < 0){
		// Error
#ifndef DISABLE_ERRNO
		dstream<<(int)m_handle<<": Select failed: "<<strerror(errno)<<std::endl;
#endif
#ifdef _WIN32
		int e = WSAGetLastError();
		dstream<<(int)m_handle<<": WSAGetLastError()="<<e<<std::endl;
		if(e == 10004 /*=WSAEINTR*/)
		{
			dstream<<"WARNING: Ignoring WSAEINTR."<<std::endl;
			return false;
		}
#endif
		throw SocketException("Select failed");
	}
	else if(FD_ISSET(m_handle, &readset) == false){
		// No data
		//dstream<<"Select reported no data in m_handle"<<std::endl;
		return false;
	}
	
	// There is data
	//dstream<<"Select reported data in m_handle"<<std::endl;
	return true;
}


