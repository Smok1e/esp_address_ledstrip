idf_component_register(
	SRCS
		"src/Main.cpp"
		"src/Buffer.cpp"
		"src/Utils.cpp"
		"src/Client.cpp"
		
	INCLUDE_DIRS
		"include/"
	
	REQUIRES
		esp_wifi
		nvs_flash
		esp_driver_gpio
		led_strip
		esp_timer
		lua
)