#include <Client.hpp>
#include <Main.hpp>

#include <utility>

//========================================

constexpr size_t RECV_BUFFSIZE = 128;

//========================================

Client::Client(Main* main, int socket):
	m_main(main),
	m_socket(socket)
{}

//========================================

void Client::startTask()
{
	xTaskCreate(
		[](void* instance) -> void
		{
			reinterpret_cast<Client*>(instance)->serve();
		},
		"Client",
		4096,
		this,
		5,
		nullptr
	);
}

void Client::serve()
{
	sockaddr_in client_addr = {};
	socklen_t client_addr_len = sizeof(client_addr);
	getpeername(m_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);

	inet_ntop(AF_INET, &client_addr.sin_addr, m_address, std::size(m_address));
	
	ESP_LOGI(m_address, "client connected");
	
	uint8_t buffer[RECV_BUFFSIZE] = {};
	ssize_t len;
	while ((len = recv(m_socket, buffer, sizeof(buffer), 0)) > 0)
	{
#if CONFIG_SERVER_VERBOSE
		ESP_LOGI(m_address, "received %zu bytes:", static_cast<size_t>(len));
		DumpHex(buffer, len);
#endif
		
		m_incoming_packet.write(buffer, len);
		processData();
	}
	
	if (!len)
		ESP_LOGI(m_address, "client disconnected");
	
	else
		ESP_LOGI(m_address, "connection lost: %s", strerror(errno));
	
	close(m_socket);
	vTaskDelete(nullptr);
	
	m_main->deleteClient(this);
}

void Client::processData()
{
	if (m_pending_packet_length)
	{
		if (m_incoming_packet.remain() >= m_pending_packet_length)
		{
			processPacket(m_pending_packet_length);
			m_pending_packet_length = 0;
		}
		
		else
		{
#ifdef CONFIG_SERVER_VERBOSE
			ESP_LOGI(
				m_address,
				"packet still received partially (expecting %zu more bytes)",
				m_pending_packet_length - m_incoming_packet.remain()
			);
#endif
			
			return;
		}
	}
	
	while (m_incoming_packet.remain() >= 4)
	{
		size_t packet_length = m_incoming_packet.readUint32();
		
		if (m_incoming_packet.remain() >= packet_length)
			processPacket(packet_length);
		
		else
		{
#ifdef CONFIG_SERVER_VERBOSE
			ESP_LOGI(
				m_address,
				"packet received partially (expecting %zu more bytes, %zu total)",
				packet_length - m_incoming_packet.remain(),
				packet_length
			);
#endif
			
			m_pending_packet_length = packet_length;
			return;
		}
	}
}

void Client::processPacket(size_t length)
{
	ESP_LOGI(m_address, "received full packet (%zu bytes)", length);
	auto offset = m_incoming_packet.offset();
	
	auto message_id = static_cast<Message>(m_incoming_packet.readUint8());
	switch (message_id)
	{
		case Message::DEBUG_MESSAGE:
			onMessageDebug();
			break;
			
		case Message::EXECUTE_PROGRAM:
			onMessageExecuteProgram();
			break;
			
		case Message::ERASE_PROGRAM:
			onMessageEraseProgram();
			break;
			
		default:
			ESP_LOGW(m_address, "unknown message type 0x%02X", static_cast<uint8_t>(message_id));
			break;
			
	}
	
	m_incoming_packet.seek(offset + length, true);
	m_incoming_packet.discard();
}

//========================================

void Client::sendPacket()
{
	ESP_LOGI(m_address, "sending packet (%zu bytes)", m_outgoing_packet.size());
	
	Buffer packet;
	packet.writeUint32(m_outgoing_packet.size());
	packet.write(m_outgoing_packet.data(), m_outgoing_packet.size());
	
#ifdef CONFIG_SERVER_VERBOSE
	ESP_LOGI(m_address, "sending %zu bytes:", packet.size());
	DumpHex(packet.data(), packet.size());
#endif

	size_t sent = 0;
	while (sent < packet.size())
	{
		ssize_t len = send(
			m_socket,
			packet.data() + sent,
			packet.size() - sent,
			0
		);
		
		if (len < 0)
		{
			ESP_LOGI(m_address, "send() failed: %s", strerror(errno));
			return;
		}
		
		sent += len;
	}
	
	m_outgoing_packet.clear();
}

//========================================

void Client::onMessageDebug()
{
	auto text = m_incoming_packet.readString();
	ESP_LOGI(m_address, "received debug message: '%.*s'", text.length(), text.data());
}

void Client::onMessageExecuteProgram()
{
	auto save = m_incoming_packet.readBoolean();
	auto source = m_incoming_packet.readString();
	auto [size, units] = FormatBytes(source.size());
	
	ESP_LOGI(
		m_address,
		"received program (%.2f %s), save %srequested",
		size,
		units,
		save
			? ""
			: "not "
	);
	
	char error[512] = "";
	if (m_main->executeProgram(source, error, sizeof(error)))
	{
		if (save)
		{
			m_main->saveProgram(source);
			ESP_LOGI(m_address, "program saved");
		}
		
		sendMessageExecuteProgramSuccess();
	}
	
	else
		sendMessageExecuteProgramFailure(error);
}

void Client::onMessageEraseProgram()
{
	ESP_LOGI(m_address, "received program erase request");
	m_main->saveProgram(std::nullopt);
}

//========================================

void Client::sendMessageExecuteProgramSuccess()
{
	ESP_LOGI(m_address, "reporting program execution success");
	
	m_outgoing_packet.writeUint8(std::to_underlying(Message::EXECUTE_PROGRAM_SUCCESS));
	sendPacket();
}

void Client::sendMessageExecuteProgramFailure(std::string_view reason)
{
	ESP_LOGI(m_address, "reporting program execution failure");
	
	m_outgoing_packet.writeUint8(std::to_underlying(Message::EXECUTE_PROGRAM_FAILURE));
	m_outgoing_packet.writeString(reason);
	sendPacket();
}

//========================================