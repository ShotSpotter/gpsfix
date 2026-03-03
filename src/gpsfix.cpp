/* SPDX-License-Identifier: MIT
 *
 * gpsfix.cpp
 *
 * Simple command-line tool to monitor GPS data from gpsd (https://gpsd.gitlab.io/gpsd/)
 *
 * Robert B. Calhoun <rcalhoun@shotspotter.com>
 *
 * (c) 2026 by SoundThinking, Inc.
 * Released under the MIT License
 *
 */

#include <iostream>
#include <format>
#include <ctime>
#include <unistd.h>
#include <libgpsmm.h>
#include <CLI/CLI.hpp>

#define VERSION "1.0.0"
#define WAIT_TIME_MICROSECONDS 10000

int main(int argc, char* argv[]) {
	CLI::App app{"gpsfix - read and display GPS data from gpsd"};
	app.set_version_flag("-v,--version", VERSION);

	int numReads = 0;  // 0 means infinite
	std::string host = "localhost";
	std::string port = DEFAULT_GPSD_PORT;
	bool jsonOutput = false;
	bool quiet = false;
	bool waitForFix = false;

	// Note: .default_val() and .default_str() not supported by BR2_PACKAGE_CLI11 in buildroot 2024.08
	app.add_option("-n,--num-reads", numReads, "Number of readings (0 for continuous, default: 0)");
	app.add_option("--host", host, "GPSD host (default: localhost)");
	app.add_option("--port", port, "GPSD port (default: 2947)");
	app.add_flag("-j,--json", jsonOutput, "Output JSON lines (machine-readable)");
	app.add_flag("-q,--quiet", quiet, "Suppress informational messages");
	app.add_flag("-w,--wait", waitForFix, "Wait for valid GPS fix before reporting");

	CLI11_PARSE(app, argc, argv);

	// Connect to gpsd
	gpsmm gps_rec(host.c_str(), port.c_str());
	gps_data_t* data = gps_rec.stream(WATCH_ENABLE | WATCH_JSON);

	if (data == nullptr) {
		std::cerr << "Error: Failed to connect to gpsd at " << host << ":" << port << std::endl;
		return 1;
	}

	if (!quiet) {
		std::cerr << "Connected to gpsd at " << host << ":" << port << std::endl;
	}

	// Try to read device information
	bool deviceFound = false;
	for (int i = 0; i < 20 && !deviceFound; ++i) {
		if (gps_rec.waiting(100000)) {  // 100ms wait
			data = gps_rec.read();
			if (data != nullptr && data->devices.ndevices >= 1) {
				if (!quiet) {
					auto& dev = data->devices.list[0];
					std::cerr << "Found GPS device: " << dev.path;
					if (dev.driver[0] != '\0') {
						std::cerr << " (" << dev.driver;
						if (dev.subtype[0] != '\0') {
							std::cerr << ", " << dev.subtype;
						}
						std::cerr << ")";
					}
					if (dev.baudrate > 0) {
						std::cerr << " @ " << dev.baudrate << dev.parity << dev.stopbits;
					}
					std::cerr << std::endl;
				}
				deviceFound = true;
			}
		} else {
			usleep(100000);
		}
	}

	if (!deviceFound && !quiet) {
		std::cerr << "Note: GPS device list not populated yet (gpsd may still be working)" << std::endl;
	}

	if (!jsonOutput) {
		std::cout << std::format("{:>20} {:>6} {:>4} {:>12} {:>12} {:>10} {:>10} {:>8} {:>6} {:>5}\n",
			"Timestamp", "Count", "Mode", "Latitude", "Longitude", "HAE (m)", "MSL (m)", "PDOP", "SVs", "Fix");
		std::cout << std::string(104, '-') << std::endl;
	}

	int readCount = 0;
	time_t lastReport = 0;
	time_t startTime = time(nullptr);
	time_t lastDataTime = time(nullptr);
	const int WAIT_FOR_FIX_SECONDS = 5;  // Wait up to 5 seconds for first fix
	const int NO_DATA_TIMEOUT_SECONDS = 10;  // Exit if no data received for 10 seconds
	bool waitingForFirstFix = waitForFix;  // Only wait if --wait flag specified

	while (numReads == 0 || readCount < numReads) {
		// Check for timeout if no data received
		time_t now = time(nullptr);
		if (now - lastDataTime > NO_DATA_TIMEOUT_SECONDS) {
			std::cerr << "Error: No data received from gpsd for " << NO_DATA_TIMEOUT_SECONDS << " seconds" << std::endl;
			return 1;
		}

		if (gps_rec.waiting(WAIT_TIME_MICROSECONDS)) {
			data = gps_rec.read();
			if (data != nullptr) {
				// Update last data time
				lastDataTime = time(nullptr);

				// Wait for a valid fix before starting to report, but timeout after 5 seconds
				if (waitingForFirstFix) {
					time_t elapsed = time(nullptr) - startTime;
					if (data->fix.mode >= MODE_2D && (data->set & LATLON_SET)) {
						// Got a fix, start reporting
						waitingForFirstFix = false;
						lastReport = time(nullptr);
					} else if (elapsed >= WAIT_FOR_FIX_SECONDS) {
						// Timeout - report even without fix
						if (!jsonOutput && !quiet) {
							std::cerr << "Note: Starting reports without GPS fix (waited " << WAIT_FOR_FIX_SECONDS << "s)" << std::endl;
						}
						waitingForFirstFix = false;
						lastReport = time(nullptr);
					} else {
						// Still waiting, continue reading
						continue;
					}
				}

				// Only report once per second
				if (now > lastReport) {
					lastReport = now;
					readCount++;

					// Get UTC timestamp
					char timeStr[21];
					struct tm* tm_info;
					if (data->set & TIME_SET) {
						// Use GPS time if available
						time_t gpsTime = (time_t)data->fix.time.tv_sec;
						tm_info = gmtime(&gpsTime);
					} else {
						// Fall back to system time
						tm_info = gmtime(&now);
					}
					strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", tm_info);

					// Extract data
					int mode = data->fix.mode;  // 0=not seen, 1=no fix, 2=2D, 3=3D
					double lat = (data->set & LATLON_SET) ? data->fix.latitude : 0.0;
					double lon = (data->set & LATLON_SET) ? data->fix.longitude : 0.0;
					double hae = (data->set & ALTITUDE_SET) ? data->fix.altHAE : 0.0;
					double msl = (data->set & ALTITUDE_SET) ? data->fix.altMSL : 0.0;
					double pdop = (data->set & DOP_SET) ? data->dop.pdop : 0.0;
					int svs = data->satellites_visible;

					// Determine fix status
					std::string fixStatus = "NO_FIX";
					if (data->set & (STATUS_SET | MODE_SET)) {
						if (data->fix.mode >= MODE_2D) {
							if (data->fix.status >= STATUS_DGPS) {
								fixStatus = "DGPS";
							} else {
								fixStatus = (data->fix.mode == MODE_2D) ? "2D" : "3D";
							}
						}
					}

					// Output
					if (jsonOutput) {
						std::cout << std::format("{{\"timestamp\":\"{}\",\"count\":{},\"mode\":{},\"lat\":{:.7f},\"lon\":{:.7f},\"hae\":{:.2f},\"msl\":{:.2f},\"pdop\":{:.3f},\"svs\":{},\"fix\":\"{}\"}}\n",
							timeStr, readCount, mode, lat, lon, hae, msl, pdop, svs, fixStatus);
					} else {
						std::cout << std::format("{:>20} {:6d} {:4d} {:12.7f} {:12.7f} {:10.2f} {:10.2f} {:8.3f} {:6d}  {}\n",
							timeStr, readCount, mode, lat, lon, hae, msl, pdop, svs, fixStatus);
					}
					std::cout.flush();
				}
			}
		} else {
			// No data available, sleep to avoid tight loop
			usleep(WAIT_TIME_MICROSECONDS);
		}
	}

	if (!jsonOutput) {
		std::cout << "\nCompleted " << readCount << " reading(s)" << std::endl;
	}
	return 0;
}
