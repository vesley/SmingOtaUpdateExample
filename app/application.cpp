#include <SmingCore.h>
#include <Ota/Network/HttpUpgrader.h>
#include <Storage/PartitionStream.h>
#include <Storage/SpiFlash.h>
#include <Ota/Upgrader.h>

// If you want, you can define WiFi settings globally in Eclipse Environment Variables
#ifndef WIFI_SSID
#define WIFI_SSID "PleaseEnterSSID" // Put your SSID and password here
#define WIFI_PWD "PleaseEnterPass"
#endif

Ota::Network::HttpUpgrader* otaUpdater;
OtaUpgrader ota;
HttpServer server;

Storage::Partition findSpiffsPartition()
{
	String name = F("spiffs0");
	auto part = Storage::findPartition(name);
	if(!part) {
		debug_w("Partition '%s' not found", name.c_str());
	}
	return part;
}

void upgradeCallback(Ota::Network::HttpUpgrader& client, bool result)
{
	Serial.println("In callback...");
	if(result == true) {
		// success
		ota.end();

		auto part = ota.getNextBootPartition();
		// set to boot new rom and then reboot
		Serial.printf(_F("Firmware updated, rebooting to %s @ ...\r\n"), part.name().c_str());
		ota.setBootPartition(part);
		System.restart();
	} else {
		ota.abort();
		// fail
		Serial.println(_F("Firmware update failed!"));
	}
}

void upgradeSpiffsCallback(Ota::Network::HttpUpgrader& client, bool result) {
	if (result == true) {
		// success
		ota.end();

		Serial.printf(_F("SPIFFS rom updated, rebooting...\r\n"));
		System.restart();
	} else {
		ota.abort();
		// fail
		Serial.println(_F("SPIFFS update failed\r\n"));
	}
}

void doUpgrade(String &romUrl, String &spiffsUrl) {
	// need a clean object, otherwise if run before and failed will not run again
	if (otaUpdater) {
		delete otaUpdater;
	}
	otaUpdater = new Ota::Network::HttpUpgrader();

	if (romUrl != nullptr) {
		// select rom slot to flash
		auto part = ota.getNextBootPartition();
		Serial.printf("Part %s \r\n", part.name().c_str());

		otaUpdater->addItem(romUrl, part);

		ota.begin(part);
	}

	if (spiffsUrl != nullptr) {
		// unmount spiffs partition before update
		if(fileSystemType() == IFS::IFileSystem::Type::SPIFFS) {
			fileFreeFileSystem();
		}
		auto spiffsPart = findSpiffsPartition();
		// use user supplied values (defaults for 4mb flash in hardware config)
		otaUpdater->addItem(spiffsUrl, spiffsPart, new Storage::PartitionStream(spiffsPart));
	}

	// request switch and reboot on success
	if (romUrl != nullptr) {
		otaUpdater->setCallback(upgradeCallback);
	} else if (spiffsUrl != nullptr) {
		otaUpdater->setCallback(upgradeSpiffsCallback);
	}

	// start update
	otaUpdater->start();

}

void handleOtaUpdate(HttpRequest& request, HttpResponse& response) {
	if (request.method == HTTP_GET) {
		response.headers[HTTP_HEADER_CACHE_CONTROL] = F("no-chache, no-store");
		response.headers[F("Pragma")] = F("no-cache");
		response.headers[HTTP_HEADER_CONTENT_TYPE] = toString(MIME_HTML);

		String output = F("<html><body><form action=\"/otaUpdate\" method=\"post\" enctype=\"application/x-www-form-urlencoded\">Application rom URL: ");
    	output += F("<input type=\"text\" name=\"rom_url\"><br>SPIFFS rom URL: <input type=\"text\" name=\"spiffs_url\"><br>");
		output += F("<input class=\"button\" type=\"submit\" value=\"OTA Update\">");
		output += F("</form></body></html>");
		response.sendString(output);
	} else if (request.method == HTTP_POST) {
		String romUrl = request.getPostParameter("rom_url");
		String spiffsUrl = request.getPostParameter("spiffs_url");

		doUpgrade(romUrl, spiffsUrl);

		response.headers[HTTP_HEADER_CONTENT_TYPE] = toString(MIME_TEXT);
		response.sendString(F("done"));
	} else {
		response.headers[HTTP_HEADER_CONTENT_TYPE] = toString(MIME_TEXT);
		response.code = HttpStatus::METHOD_NOT_ALLOWED;
		response.sendString(F("method not allowed"));
	}
}

bool sendFile(HttpRequest& request, HttpResponse& response) {
	String path = request.uri.getRelativePath();

	response.headers[HTTP_HEADER_CACHE_CONTROL] = F("no-chache, no-store");
	response.headers[F("Pragma")] = F("no-cache");

	String pathWithGz = path + ".gz";
	if (fileExist(pathWithGz) || fileExist(path)) {
		response.setCache(86400, true); // It's important to use cache for better performance.
		response.sendFile(path);
		return true;
	} else {
		response.code = HTTP_STATUS_NOT_FOUND;
		response.sendString(F("404: Not Found"));
	}
	return false;
}

void init()
{
	Serial.begin(SERIAL_BAUD_RATE); // 115200 by default
	Serial.systemDebugOutput(true); // Debug output to serial

	// mount spiffs
	auto partition = ota.getRunningPartition();
	auto spiffsPartition = findSpiffsPartition();
	if (spiffsPartition) {
		debugf("trying to mount %s @ 0x%08x, length %d", spiffsPartition.name().c_str(), spiffsPartition.address(),
			   spiffsPartition.size());
		spiffs_mount(spiffsPartition);
	}

	WifiAccessPoint.enable(false);
	WifiStation.config(WIFI_SSID, WIFI_PWD);
	WifiStation.enable(true);
	WifiStation.connect();

	Serial.printf(_F("\r\nCurrently running %s @ 0x%08lx.\r\n"), partition.name().c_str(), partition.address());
	Serial.println();

	// Start web server
	server.listen(80);

	// Register URL
	server.paths.set(F("/otaUpdate"), handleOtaUpdate);
	server.paths.setDefault(sendFile);
}
