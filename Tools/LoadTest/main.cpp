
#include <memory>
#include <vector>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <iostream>
#include <sstream>
#include <unistd.h>

#define SharedPtr std::shared_ptr
void PrintStackTrace() {}

typedef std::string AString;
typedef std::vector<std::string> AStringVector;
typedef signed long long Int64;
typedef signed int       Int32;
typedef signed short     Int16;
typedef signed char      Int8;

typedef unsigned long long UInt64;
typedef unsigned int       UInt32;
typedef unsigned short     UInt16;
typedef unsigned char      UInt8;

void LOGERROR(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

void LOGWARN(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

void LOG(const char * a_Format, ...)
{
	va_list argList;
	va_start(argList, a_Format);
	vprintf(a_Format, argList);
	putchar('\n');
	va_end(argList);
}

#include "Globals.h"
#include "../../src/OSSupport/Network.h"
#include "../../src/ByteBuffer.h"


class ConnectionCallbacks : public cNetwork::cConnectCallbacks
{
	virtual void OnConnected(cTCPLink & a_Link)
	{
		std::cout << "connected" << std::endl;
	}

	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg)
	{
		std::cout << "error" << std::endl;
	}

};

class LinkCallbacks : public cTCPLink::cCallbacks
{

	enum States
	{
		LOGIN,
		GAME
	} m_State;

	int id;

public:
	LinkCallbacks(int i) : m_State(LOGIN), id(i), recivedData(1024) {}

	virtual void OnLinkCreated(cTCPLinkPtr a_Link)
	{
		cByteBuffer p(400);
		// Handshake
		p.WriteVarInt32(15);
		p.WriteVarInt32(0x0);
		p.WriteVarInt32(5);
		p.WriteVarUTF8String("Localhost");
		p.WriteBEUInt16(25565);
		p.WriteVarInt32(2);
		// Login Start

		std::stringstream sstm;
		sstm << "LoadTest" << id;
		std::string idstring = sstm.str();

		cByteBuffer payload(100);
		payload.WriteVarInt32(0x0);
		payload.WriteVarUTF8String(idstring);
		p.WriteVarInt32(payload.GetUsedSpace());
		payload.ReadToByteBuffer(p, payload.GetUsedSpace());
		std::string data;
		p.ReadAll(data);
		p.CommitRead();
		a_Link->Send(data);
	}
	virtual void OnReceivedData(const char * a_Data, size_t a_Length)
	{
		recivedData.WriteBuf(a_Data, a_Length);
		switch (m_State)
		{
			case LOGIN:
				HandleLoginPackets();
				break;
			case GAME:
				HandleGamePackets();
				break;
		}
	}

#define IGNORE_PAYLOAD recivedData.SkipRead(length - typeLength);

	void HandleLoginPackets()
	{
		for(;;) 
		{
			UInt32 length, type;
			if (!recivedData.ReadVarInt32(length))
			{
				recivedData.ResetRead();
				break;
			}
			if (!recivedData.CanReadBytes(length))
			{
				recivedData.ResetRead();
				break;
			}
			int typeLength = recivedData.GetReadableSpace();
			assert(recivedData.ReadVarInt32(type));
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x2:
				{
					std::cout << id << ": LoginSuccess" << std::endl;
					IGNORE_PAYLOAD
					recivedData.CommitRead();
					m_State = GAME;
					HandleGamePackets();
					break;
				}
				default:
				{
					std::cout << id << ": Unknown: Ignoring: " << type << std::endl;
					IGNORE_PAYLOAD
					break;
				}
			}
			recivedData.CommitRead();
		}
	}


	void HandleGamePackets()
	{
		for(;;) 
		{
			UInt32 length, type;
			if (!recivedData.ReadVarInt32(length))
			{
				recivedData.ResetRead();
				break;
			}
			if (!recivedData.CanReadBytes(length))
			{
				recivedData.ResetRead();
				break;
			}
			int typeLength = recivedData.GetReadableSpace();
			assert(recivedData.ReadVarInt32(type));
			typeLength -= recivedData.GetReadableSpace();
			switch (type)
			{
				case 0x2:
				{
					std::cout << id << ": Chat" << std::endl;
					IGNORE_PAYLOAD
					break;
				}
				default:
				{
					std::cout << id << ": Unknown: Ignoring: " << type << std::endl;
					IGNORE_PAYLOAD
					break;
				}
			}
			recivedData.CommitRead();
		}
	}

	virtual void OnRemoteClosed(void)
	{
		std::cout << id << ": closed" << std::endl;
	}
	virtual void OnError(int a_ErrorCode, const AString & a_ErrorMsg)
	{
		std::cout << id << ": error" << std::endl;
	}

private:
	cByteBuffer recivedData;

};


int main()
{
	for(int i = 0;i < 1000; i++)
	{
		std::cout << "Creating new Player" << std::endl;
		cNetwork::Connect("localhost", 25565, std::make_shared<ConnectionCallbacks>(), std::make_shared<LinkCallbacks>(i));
		usleep(100000);
	}
}
