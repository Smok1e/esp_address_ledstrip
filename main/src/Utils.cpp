#include <cstdlib>
#include <cmath>

#include <Utils.hpp>

//======================================== Network

esp_err_t StrToIP4Addr(const char* str, esp_ip4_addr* addr)
{
	for (size_t i = 0; i < 4; i++)
	{
		char* end = nullptr;
		unsigned byte = strtoul(str, &end, 10);

		if (!end || byte > 0xFF)
			return ESP_FAIL;

		reinterpret_cast<uint8_t*>(&addr->addr)[i] = byte;
		str = end + 1;
	}

	return ESP_OK;
}

int NetCheck(int status, const char* tag, const char* expr)
{
	if (status < 0)
	{
		ESP_LOGE(tag, "%s failed with errno %d: %s; Aborting", expr, errno, strerror(errno));
		abort();
	}

	return status;
}

//======================================== Misc

void DumpHex(const void* data, size_t length)
{
	constexpr size_t line_size = 16;
	const auto* bytes = reinterpret_cast<const char*>(data);
	
	for (size_t offset = 0; offset < length; offset += line_size)
	{
		printf("%04zu: ", offset);
		
		for (size_t i = 0; i < line_size; i++)
		{
			if (offset + i >= length)
			{
				printf("   ");
				continue;
			}
			
			printf("%02x ", bytes[offset + i]);
		}
		
		printf(" ");
		
		for (size_t i = 0; i < line_size && offset + i < length; i++)
		{
			auto byte = bytes[offset + i];
			printf(
				"%c",
				0x20 <= byte && byte <= 0x7E
					? byte
					: '.'
			);
		}
		
		printf("\n");
	}
}

std::pair<float, const char*> FormatBytes(size_t bytes)
{
	constexpr const char* units[] = {
		"bytes", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"
	};
	
	unsigned power = floor(log(bytes) / log(1024));
	
	return {
		static_cast<float>(bytes) / pow(1024, power),
		units[power]
	};
}

//========================================