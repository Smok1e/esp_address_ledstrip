#include <Main.hpp>
#include <Utils.hpp>
#include <Buffer.hpp>

#include <esp_timer.h>

//========================================

static const char* TAG = "main";

//======================================== Wifi

void Main::initNVS()
{
	auto result = nvs_flash_init();
	if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		result = nvs_flash_init();
	}
	ESP_ERROR_CHECK(result);
	
	ESP_LOGI(TAG, "nvs initialized");
}

void Main::initWifi()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	auto* netif = esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(esp_netif_set_hostname(netif, CONFIG_WIFI_HOSTNAME));
	
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id, instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&Main::WifiEventHandler,
		this,
		&instance_any_id
	));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&Main::WifiEventHandler,
		this,
		&instance_got_ip
	));

	wifi_config_t wifi_config = {};

#ifdef CONFIG_WIFI_SEUCURITY_WPA2
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#else
	wifi_config.sta.threshold.authmode = WIIF_AUTH_WPA3_PSK;
#endif

	strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid    ), CONFIG_WIFI_SSID,     sizeof(wifi_config.sta.ssid    ));
	strncpy(reinterpret_cast<char*>(wifi_config.sta.password), CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi station initialized");

#if	(CONFIG_WIFI_DISABLE_DHCP)
	// Disabling DHCP and setting preferred ip address
	ESP_LOGI(TAG, "stopping DHCP client");

	esp_netif_ip_info_t ip_info = {};
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_IP,      &ip_info.ip     ));
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_GATEWAY, &ip_info.gw     ));
	ESP_ERROR_CHECK(StrToIP4Addr(CONFIG_WIFI_NETMASK, &ip_info.netmask));

	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
	ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
#endif
}

void Main::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	Main* instance = reinterpret_cast<Main*>(arg);

	if      (event_base == WIFI_EVENT) instance->onWifiEvent(event_id, event_data);
	else if (event_base == IP_EVENT  ) instance->onIpEvent  (event_id, event_data);
}

void Main::onWifiEvent(int32_t event_id, void *event_data)
{
	switch (event_id)
	{
		case WIFI_EVENT_STA_START:
			esp_wifi_connect();
			ESP_LOGI(TAG, "connecting to %s...", CONFIG_WIFI_SSID);
			break;

		case WIFI_EVENT_STA_DISCONNECTED:
		{
			m_wifi_connected = false;
			
			esp_wifi_connect();
			ESP_LOGI(
				TAG,
				"wifi disconnected (%d); reconnecting to %s...",
				static_cast<int>(
					reinterpret_cast<wifi_event_sta_disconnected_t*>(event_data)->reason
				),
				CONFIG_WIFI_SSID
			);

			break;
		}
	}
}

void Main::onIpEvent(int32_t event_id, void* event_data)
{
	switch (event_id)
	{
		case IP_EVENT_STA_GOT_IP:
		{
			m_wifi_connected = true;
			
			auto* event = reinterpret_cast<ip_event_got_ip_t*>(event_data);
			ESP_LOGI(TAG, "successfully connected to the AP; got ip: " IPSTR, IP2STR(&event->ip_info.ip));
			
			break;
		}
	}
}

//======================================== Server

void Main::startTcpServer()
{
	int server = NET_CHECK(socket(AF_INET, SOCK_STREAM, 0));

	sockaddr_in server_addr = {};
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(CONFIG_SERVER_PORT);
	NET_CHECK(bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)));

	NET_CHECK(listen(server, 1));

	ESP_LOGI(TAG, "listening for connections port %d", CONFIG_SERVER_PORT);
	while (true)
	{
		int sock = NET_CHECK(accept(server, nullptr, nullptr));
		m_clients.emplace_back(new Client(this, sock))->startTask();
	}
}

void Main::deleteClient(Client* client)
{
	auto iter = std::find(m_clients.begin(), m_clients.end(), client);
	if (iter == m_clients.end())
	{
		ESP_LOGE(TAG, "attemt to remove unexisting client");
		return;
	}
	
	delete client;
	m_clients.erase(iter);
}

//======================================== Lua

void Main::initLua()
{
	m_lua = luaL_newstate();
	luaL_openlibs(m_lua);
	
	// Exposing ledstrip API
	lua_newtable(m_lua);
	
	// numLeds constant
	lua_pushnumber(m_lua, CONFIG_LEDSTRIP_LED_NUMBER);
	lua_setfield(m_lua, -2, "numLeds");
	
	// updateRate constant
	lua_pushnumber(m_lua, CONFIG_LEDSTRIP_UPDATE_RATE);
	lua_setfield(m_lua, -2, "updateRate");
	
	// setPixelHSV function
	lua_pushlightuserdata(m_lua, this);
	lua_pushcclosure(m_lua, Main::ApiSetPixelRGB, 1);
	lua_setfield(m_lua, -2, "setPixelRGB");
	
	// setPixelHSV function
	lua_pushlightuserdata(m_lua, this);
	lua_pushcclosure(m_lua, Main::ApiSetPixelHSV, 1);
	lua_setfield(m_lua, -2, "setPixelHSV");
	
	// clear function
	lua_pushlightuserdata(m_lua, this);
	lua_pushcclosure(m_lua, Main::ApiClear, 1);
	lua_setfield(m_lua, -2, "clear");
	
	lua_setglobal(m_lua, "ledstrip");
	
	ESP_LOGI(TAG, "lua initialized");
	
	std::string program;
	if (loadProgram(program))
	{
		ESP_LOGI(TAG, "loaded program (%zu bytes)", program.length());
		executeProgram(program);
	}
	
	else
		ESP_LOGI(TAG, "no saved program found");
}

int Main::ApiSetPixelRGB(lua_State* lua)
{
	auto* instance = reinterpret_cast<Main*>(lua_touserdata(lua, lua_upvalueindex(1)));
	
	int index = luaL_checkinteger(lua, 1) - 1;
	if (!(0 <= index && index < CONFIG_LEDSTRIP_LED_NUMBER))
	{
		lua_pushnil(lua);
		lua_pushstring(lua, "pixel index out of bounds");
		
		return 2;
	}
	
	int r = luaL_checkinteger(lua, 2);
	int g = luaL_checkinteger(lua, 3);
	int b = luaL_checkinteger(lua, 4);
	
	led_strip_set_pixel(instance->m_ledstrip, index, r, g, b);
	
	lua_pushboolean(lua, true);
	return 1;
}

int Main::ApiSetPixelHSV(lua_State* lua)
{
	auto* instance = reinterpret_cast<Main*>(lua_touserdata(lua, lua_upvalueindex(1)));
	
	int index = luaL_checkinteger(lua, 1) - 1;
	if (!(0 <= index && index < CONFIG_LEDSTRIP_LED_NUMBER))
	{
		lua_pushnil(lua);
		lua_pushstring(lua, "pixel index out of bounds");
		
		return 2;
	}
	
	int h = luaL_checkinteger(lua, 2);
	int s = luaL_checkinteger(lua, 3);
	int v = luaL_checkinteger(lua, 4);
	
	led_strip_set_pixel_hsv(instance->m_ledstrip, index, h, s, v);
	
	lua_pushboolean(lua, true);
	return 1;
}

int Main::ApiClear(lua_State* lua)
{
	auto* instance = reinterpret_cast<Main*>(lua_touserdata(lua, lua_upvalueindex((1))));
	led_strip_clear(instance->m_ledstrip);
	
	lua_pushboolean(lua, true);
	return 1;
}

bool Main::executeProgram(std::string_view source, char* error_buffer /*= nullptr*/, size_t error_buffer_length /*= 0*/)
{
	if (luaL_loadbuffer(m_lua, source.data(), source.length(), "program") != LUA_OK)
	{
		const char* message = lua_tostring(m_lua, -1);
		
		ESP_LOGE(TAG, "program syntax error: %s", message);
		
		if (error_buffer)
			snprintf(error_buffer, error_buffer_length, "syntax error: %s", message);
		
		lua_pop(m_lua, 1);
		return false;
	}
	
	if (lua_pcall(m_lua, 0, 0, 0) != LUA_OK)
	{
		const char* message = lua_tostring(m_lua, -1);
		
		ESP_LOGE(TAG, "program runtime error: %s", message);
		
		if (error_buffer)
			snprintf(error_buffer, error_buffer_length, "runtime error: %s", message);
		
		lua_pop(m_lua, 1);
		return false;
	}
	
	ESP_LOGI(TAG, "program executed successfully");
	m_script_failure = false;
		m_script_start_time = esp_timer_get_time();
	
	return true;
}

void Main::saveProgram(std::optional<std::string_view> source /*= std::nullopt*/)
{
	nvs_handle_t nvs_handle = 0;
	ESP_ERROR_CHECK(nvs_open(TAG, NVS_READWRITE, &nvs_handle));
	
	if (source)
	{
		ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, "program", source->data(), source->length()));
		ESP_LOGI(TAG, "program saved");
	}
	
	else
	{
		auto ret = nvs_erase_key(nvs_handle, "program");
		if (ret == ESP_ERR_NVS_NOT_FOUND)
			ESP_LOGI(TAG, "nothing to erase");
		
		else
		{
			ESP_ERROR_CHECK(ret);
			ESP_LOGI(TAG, "program erased");
		}
	}
	
	ESP_ERROR_CHECK(nvs_commit(nvs_handle));
	
	nvs_close(nvs_handle);
}

bool Main::loadProgram(std::string& program)
{
	nvs_handle_t nvs_handle = 0;
	
	auto ret = nvs_open(TAG, NVS_READONLY, &nvs_handle);
	if (ret == ESP_ERR_NVS_NOT_FOUND)
		return false;
	
	ESP_ERROR_CHECK(ret);
	
	size_t size = 0;
	
	ret = nvs_get_blob(nvs_handle, "program", nullptr, &size);
	if (ret == ESP_ERR_NVS_NOT_FOUND)
	{
		nvs_close(nvs_handle);
		return false;
	}
	
	ESP_ERROR_CHECK(ret);
	
	program.resize(size);
	
	ESP_ERROR_CHECK(nvs_get_blob(nvs_handle, "program", program.data(), &size));
	nvs_close(nvs_handle);
	
	return true;
}

//======================================== Ledstrip

void Main::initLedstrip()
{
	led_strip_config_t ledstrip_config = {};
	ledstrip_config.strip_gpio_num = static_cast<gpio_num_t>(CONFIG_LEDSTRIP_DATA_PIN);
	ledstrip_config.max_leds = CONFIG_LEDSTRIP_LED_NUMBER;
	ledstrip_config.led_model = LED_MODEL_WS2812;
	ledstrip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
	ledstrip_config.flags.invert_out = false;
	
	led_strip_rmt_config_t rmt_config = {};
	rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
	rmt_config.resolution_hz = 10'000'000;
	rmt_config.mem_block_symbols = 64;
	rmt_config.flags.with_dma = false;
	
	ESP_ERROR_CHECK(led_strip_new_rmt_device(&ledstrip_config, &rmt_config, &m_ledstrip));
	
	ESP_LOGI(TAG, "ledstrip initialized");
}

void Main::startLedstripHandler()
{
	xTaskCreate(
		[](void* arg) -> void
		{
			reinterpret_cast<Main*>(arg)->ledstripHandler();
		},
		"Ledstrip handler",
		4096,
		this,
		5,
		nullptr
	);
}

void Main::ledstripHandler()
{
	ESP_LOGI(TAG, "ledstrip handler started");
	
	while (true)
	{
		if (!m_script_failure)
		{
			lua_getglobal(m_lua, "update");
			lua_pushnumber(m_lua, static_cast<float>(esp_timer_get_time() - m_script_start_time) / 1'000'000);
			
			if (lua_pcall(m_lua, 1, 0, 0) != LUA_OK)
			{
				ESP_LOGE(TAG, "update script failed: %s", lua_tostring(m_lua, -1));
				lua_pop(m_lua, 1);
				
				m_script_failure = true;
			}
		}
		
		ESP_ERROR_CHECK(led_strip_refresh(m_ledstrip));
		vTaskDelay(pdMS_TO_TICKS(1000 / CONFIG_LEDSTRIP_UPDATE_RATE));
	}
}

//========================================

void Main::run()
{
	initNVS();
	initWifi();
	initLua();
	initLedstrip();
	
	startLedstripHandler();
	startTcpServer();
}

//========================================

extern "C" void app_main()
{
	Main instance;
	instance.run();
}

//========================================