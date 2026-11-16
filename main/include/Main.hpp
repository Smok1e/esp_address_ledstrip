#pragma once

#include <array>
#include <mutex>
#include <memory>

#include <FreeRTOS/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_event.h>

#include <lwip/err.h>
#include <lwip/sockets.h>

#include <nvs_flash.h>

extern "C"
{
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <led_strip.h>
#include <led_strip_rmt.h>

#include <Client.hpp>

//========================================

class Main
{
public:
	friend class Client;
	
	Main() = default;

	void run();

private:
	static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
	void onWifiEvent(int32_t event_id, void* event_data);
	void onIpEvent(int32_t event_id, void* event_data);

	void startTcpServer();
	void deleteClient(Client* client);
	
	static int ApiSetPixelRGB(lua_State* lua);
	static int ApiSetPixelHSV(lua_State* lua);
	static int ApiClear      (lua_State* lua);
	
	bool executeProgram(std::string_view source, char* error_buffer = nullptr, size_t error_buffer_length = 0);
	void saveProgram(std::optional<std::string_view> source = std::nullopt);
	bool loadProgram(std::string& program);
	
	void startLedstripHandler();
	void ledstripHandler();
	
	void initNVS();
	void initWifi();
	void initLua();
	void initLedstrip();
	
	bool m_wifi_connected = false;
	std::vector<Client*> m_clients {};
	
	lua_State* m_lua = nullptr;
	bool m_script_failure = false;
	int64_t m_script_start_time = 0;
	
	led_strip_handle_t m_ledstrip = nullptr;
	
};

//========================================