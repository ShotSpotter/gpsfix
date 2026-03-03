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
#include <cmath>
#include <ctime>
#include <unistd.h>
#include <libgpsmm.h>
#include <CLI/CLI.hpp>

#define VERSION "1.0.2"
#define WAIT_TIME_MICROSECONDS 10000

int main(int argc, char* argv[]) {
	CLI::App app{"gpsfix - read and display GPS data from gpsd"};
	app.set_version_flag("-v,--version", VERSION);

	int numReads = 0;  // 0 means infinite
	std::string host = "localhost";
	std::string port = DEFAULT_GPSD_PORT;
	bool jsonOutput = false;
	bool quiet = false;

	// Note: .default_val() and .default_str() not supported by BR2_PACKAGE_CLI11 in buildroot 2024.08
	app.add_option("-n,--num-reads", numReads, "Number of readings (0 for continuous, default: 0)");
	app.add_option("--host", host, "GPSD host (default: localhost)");
	app.add_option("--port", port, "GPSD port (default: 2947)");
	app.add_flag("-j,--json", jsonOutput, "Output JSON lines (machine-readable)");
	app.add_flag("-q,--quiet", quiet, "Suppress informational messages");

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
	time_t lastDataTime = time(nullptr);
	// satellites_visible starts at 0 and is only updated by SKY messages,
	// so it must be persisted manually across reads.
	int lastSvs = 0;
	const int NO_DATA_TIMEOUT_SECONDS = 10;  // Exit if no data received for 10 seconds

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

				// Accumulate satellite count on every message, not just at report time.
				// SKY messages (which carry satellites_visible) can arrive between report
				// boundaries and would be silently discarded if we only updated inside
				// the rate-limit block.
				if (data->satellites_visible > 0)
					lastSvs = data->satellites_visible;

				// Only report once per second
				if (now > lastReport) {
					// Skip reporting if no TPV has been received yet (mode 0 = MODE_NOT_SEEN),
					// or if no SKY has been received yet (lastSvs == 0); both produce
					// uninformative all-zeros rows. Don't advance lastReport so the first
					// complete report fires immediately once both arrive.
					if (data->fix.mode == MODE_NOT_SEEN || lastSvs == 0)
						continue;

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

					// Extract data.
					// Per libgps docs, gps_data_t fields accumulate state across reads;
					// data->set reflects only the current message but underlying fields persist.
					// Use std::isfinite() to validate floats rather than relying on data->set bits.
					// satellites_visible is updated on every incoming message (above) because
					// SKY messages can arrive between report boundaries.
					int mode = data->fix.mode;
					double lat = std::isfinite(data->fix.latitude)  ? data->fix.latitude  : 0.0;
					double lon = std::isfinite(data->fix.longitude) ? data->fix.longitude : 0.0;
					double hae = std::isfinite(data->fix.altHAE)    ? data->fix.altHAE    : 0.0;
					double msl = std::isfinite(data->fix.altMSL)    ? data->fix.altMSL    : 0.0;
					double pdop = std::isfinite(data->dop.pdop)     ? data->dop.pdop      : 0.0;
					int svs = lastSvs;

					// Determine fix status
					std::string fixStatus = "NO_FIX";
					if (mode >= MODE_2D) {
						if (data->fix.status >= STATUS_DGPS) {
							fixStatus = "DGPS";
						} else {
							fixStatus = (mode == MODE_2D) ? "2D" : "3D";
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
