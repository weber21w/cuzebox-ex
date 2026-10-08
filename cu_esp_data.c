#include "cu_esp.h"

/* max string length is 128 for version info, according to firmware source code */
const char start_up_string[] =
	"ets Dec 12 2025,rst cause 4, boot mode(3,7)\r\n\n"
	"wdt reset\r\n"
	"load 0x401000000,len 24444,room 16\r\n"
	"chksum 0xe0 ho 0 tail 12 room 4\r\n"
	"ready\r\n";

const char at_gmr_string[] =
	"AT Version:CUzeBox ESP8266 AT Driver 1.7\r\n"
	"SDK version: ESP8266 SDK 1.2.0\r\n"
	"uzebox.org\r\n"
	"Build:1.0\r\n";

const char * const fake_ap_name[ESP_NUM_FAKE_APS] = {
	"Linksys01958",
	"The Promised LAN!!",
	"A Linksys To The Past",
	"Put Your Best Port Forward",
	"Cats Against Schrodinger",
	"Pretty Fly For A Wifi",
	"TellMyWifiLoveHer",
	"CenturyLink22840",
	"LANDownUnder",
	"NetGear009",
};

const char * const fake_ap_mac[ESP_NUM_FAKE_APS] = {
	"02:25:9C:3A:7F:B2",
	"02:25:9C:3A:7F:B3",
	"02:25:9C:3A:7F:B4",
	"02:25:9C:3A:7F:B5",
	"02:25:9C:3A:7F:B6",
	"02:25:9C:3A:7F:B7",
	"02:25:9C:3A:7F:B8",
	"02:25:9C:3A:7F:B9",
	"02:25:9C:3A:7F:BA",
	"02:25:9C:3A:7F:BB",
};

const char default_sntp_server0[] = "cn.ntp.org.cn";
const char default_sntp_server1[] = "ntp.sjtu.edu.cn";
const char default_sntp_server2[] = "us.pool.ntp.org";