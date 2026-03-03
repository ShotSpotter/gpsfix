# gpsfix - Command-line GPS Monitoring Tool

`gpsfix` is a aimple command-line utility to monitor GPS data from [gpsd](https://gpsd.gitlab.io/gpsd/).

GPSD includes a handful of samples clients (`cgps`, `xgps`, `gpsmon`) but none of them provide
an easy way to read the current fix and dump it to `stdout` like this:

```
$ ./gpsfix -q -w -j -n1
{"timestamp":"2026-03-03T03:07:12Z","count":1,"mode":3,"lat":41.2901729,"lon":-82.2266160,"hae":216.44,"msl":250.88,"pdop":1.170,"svs":32,"fix":"DGPS"}
```

## Options

- `-n, --num-reads <count>` - Number of readings (0 for continuous, default: 0)
- `--host <hostname>` - GPSD host (default: localhost)
- `--port <port>` - GPSD port (default: 2947)
- `-j, --json` - Output JSON lines (machine-readable)
- `-q, --quiet` - Suppress informational messages
- `-w, --wait` - Wait for valid GPS fix before reporting (default: report immediately)
- `-v, --version` - Show version information

## Usage Examples

```bash
# Show help
./gpsfix --help

# Continuous monitoring (Ctrl-C to stop)
./gpsfix

# Take 10 readings at 1/second
./gpsfix -n 10

# Connect to remote gpsd
./gpsfix --host 192.168.1.100 --port 2947

# Suppress all informational messages and return the first valid fix in json format, then exit:
./gpsfix -q -w -j -n1

```

GPSD doesn't appear to cache the last valid fix; `gps_data_t` is invalid until the next fix is reported by the hardware. By default, `gpsfix` reports whatever gpsd provides; this may indicate `NO_FIX` even though the gps itself actually has a fix:
```
$ ./gpsfix
Connected to gpsd at localhost:2947
Found GPS device: /dev/ttymxc0 (u-blox, SW ROM CORE 3.01 (107888),HW 00080000) @ 115200N1
           Timestamp  Count Mode     Latitude    Longitude    HAE (m)    MSL (m)     PDOP    SVs   Fix
--------------------------------------------------------------------------------------------------------
2026-03-03T03:12:11Z      1    0    0.0000000    0.0000000       0.00       0.00    0.000      0  NO_FIX
2026-03-03T03:12:12Z      2    0    0.0000000    0.0000000       0.00       0.00    1.120     32  NO_FIX
2026-03-03T03:12:12Z      3    3   41.2901353  -82.2266007     214.28     248.72    1.100     32  DGPS
2026-03-03T03:12:13Z      4    3   41.2901356  -82.2266001     214.31     248.75    1.100     32  DGPS
```

These bogus fixes can be filtered with the `-w, --wait` option, which up to 5 seconds for a valid GPS fix (mode >= 2) before starting to report. If no fix within 5 seconds, it begins reporting anyway.

`gpsfix`  returns both elevation data in both HAE and MSL. HAE is the natural elevation measure
in the GPS system, but Google Earth and many other tools expect MSL. The difference between the
two is the geoid height at (lat, long).

## Build

```bash
make clean && make
sudo make install
```

## Output

Reports once per second:
- Timestamp: GPS timestamp (or system time if GPS time unavailable)
- Count: Reading number
- Mode: GPS fix mode (0=not seen, 1=no fix, 2=2D fix, 3=3D fix)
- Latitude: Decimal degrees
- Longitude: Decimal degrees
- HAE: Height above ellipsoid (meters)
- MSL: Height above mean sea level (meters)
- PDOP: Position dilution of precision
- SVs: Number of satellites visible
- Fix status: `NO_FIX`, `2D`, `3D`, or `DGPS`

Note: For PPS (pulse-per-second) testing, use the `ppstest` utility.

## Example Output

### Tabular output (default)

```
Connected to gpsd at localhost:2947
Found GPS device: /dev/ttyUSB0 (u-blox, u-blox 8) @ 9600N1
           Timestamp  Count Mode     Latitude    Longitude   HAE (m)   MSL (m)     PDOP    SVs   Fix
--------------------------------------------------------------------------------------------------------
2026-03-03T12:34:56Z      1    3   37.7749295 -122.4194155     15.34     -3.21    1.234     12  3D
2026-03-03T12:34:57Z      2    3   37.7749296 -122.4194156     15.36     -3.19    1.232     12  3D
2026-03-03T12:34:58Z      3    3   37.7749297 -122.4194157     15.38     -3.17    1.230     13  3D
```

### JSON output

JSON output in enabled with `-j, --json`. Commentary goes to stderr, data to stdout as JSON lines (ndjson):

```bash
$ gpsfix -n 3 --json
{"timestamp":"2026-03-03T12:34:56Z","count":1,"mode":3,"lat":37.7749295,"lon":-122.4194155,"hae":15.34,"msl":-3.21,"pdop":1.234,"svs":12,"fix":"3D"}
{"timestamp":"2026-03-03T12:34:57Z","count":2,"mode":3,"lat":37.7749296,"lon":-122.4194156,"hae":15.36,"msl":-3.19,"pdop":1.232,"svs":12,"fix":"3D"}
{"timestamp":"2026-03-03T12:34:58Z","count":3,"mode":3,"lat":37.7749297,"lon":-122.4194157,"hae":15.38,"msl":-3.17,"pdop":1.230,"svs":13,"fix":"3D"}
```

Parse json output with `jq`:
```bash
# Get lat/lon only after fix acquired
gpsfix -n1 --wait --json --quiet | jq -r '"\(.lat), \(.lon)"'

# Check fix status
gpsfix -n1 --json --quiet | jq -r '.fix'

# Shell script example
if gpsfix -n1 -jq 2>/dev/null | jq -e '.mode >= 2' >/dev/null; then
    echo "GPS has fix"
else
    echo "No GPS fix"
fi
```

## Dependencies

- libgps (gpsd client library) - package: libgps-dev
- [CLI11](https://github.com/CLIUtils/CLI11) (header-only CLI parser) - package: libcli-dev
- C++23, primarily so that we can use [std::format](https://en.cppreference.com/w/cpp/utility/format/format.html).

## Similar Projects

For a python client, see [gpsdclient](https://github.com/tfeldmann/gpsdclient/tree/main)
