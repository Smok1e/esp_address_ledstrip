#pragma once

#include <Buffer.hpp>
#include <Utils.hpp>

//========================================

class Main;

class Client
{
public:
	enum class Message: uint8_t
	{
		DEBUG_MESSAGE           = 0,
		EXECUTE_PROGRAM         = 1,
		EXECUTE_PROGRAM_SUCCESS = 2,
		EXECUTE_PROGRAM_FAILURE = 3,
		ERASE_PROGRAM           = 4
	};
	
	Client(Main* main, int socket);
	
	void startTask();
	
private:
	Main*  m_main;
	int    m_socket;
	Buffer m_incoming_packet {};
	Buffer m_outgoing_packet {};
	char   m_address[MAX_IP4_LEN] {};
	size_t m_pending_packet_length = 0;
	
	void serve();
	void processData();
	void processPacket(size_t length);
	
	void sendPacket();
	
	void onMessageDebug();
	void onMessageExecuteProgram();
	void onMessageEraseProgram();
	
	void sendMessageExecuteProgramSuccess();
	void sendMessageExecuteProgramFailure(std::string_view reason);
	
};

//========================================