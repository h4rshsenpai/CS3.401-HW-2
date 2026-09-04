#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unordered_map>
#include <queue>
#include <algorithm>
#include <cmath>
#include <climits>

using namespace std;

#define INF_VAL 1e18

struct Measurement {
    long long timestamp, station_id;
    double temperature, humidity, pressure;
    double rainfall;
    double wind_speed;
};

struct StationStats {
    long long count = 0;
    double sum_temperature = 0.0;
    double sum_rainfall = 0.0;
};

struct StationSummary {
    long long station_id, count;
    double sum_temperature;
    double sum_rainfall;
};

struct CompareTopStations {
    bool operator()(const StationSummary& a,
                    const StationSummary& b) const {

        if (a.count != b.count)
            return a.count > b.count;   // Min-heap

        return a.station_id < b.station_id;
    }
};

int main(int argc, char** argv) {

    char* file_name =
        (argc > 1) ? argv[1] : (char*)"data.txt";

    FILE* fp = fopen(file_name, "r");

    if (!fp) {
        printf("Error opening file.\n");
        return 1;
    }

    int total_measurements = 0;
    int top_k = 0;
    int total_stations = 0;

    if (fscanf(fp, "%d %d %d",
               &total_measurements,
               &top_k,
               &total_stations) != 3) {

        printf("Error reading metadata.\n");
        fclose(fp);
        return 1;
    }

    // ------------------------------------------------------------
    // Global aggregators
    // ------------------------------------------------------------

    long long total_count = 0;
    long long extreme_temp_events = 0;

    double sum_temp = 0.0;
    double sum_humidity = 0.0;
    double sum_pressure = 0.0;
    double sum_rainfall = 0.0;
    double sum_wind = 0.0;

    double min_temp = INF_VAL;
    double max_temp = -INF_VAL;

    double min_humidity = INF_VAL;
    double max_humidity = -INF_VAL;

    double min_pressure = INF_VAL;
    double max_pressure = -INF_VAL;

    double max_rainfall = -INF_VAL;
    double max_wind = -INF_VAL;

    Measurement hottest{};
    Measurement coldest{};

    bool got_first = false;

    // ------------------------------------------------------------
    // Hash maps
    // ------------------------------------------------------------

    unordered_map<long long, StationStats> station_map;
    unordered_map<long long, long long> interval_map;

    long long ts;
    long long s_id;

    double t;
    double h;
    double p;
    double rainfall;
    double w;

    // ------------------------------------------------------------
    // Read measurements
    // ------------------------------------------------------------

    while (fscanf(fp,
                  "%lld %lld %lf %lf %lf %lf %lf",
                  &ts,
                  &s_id,
                  &t,
                  &h,
                  &p,
                  &rainfall,
                  &w) == 7) {

        Measurement curr = {
            ts,
            s_id,
            t,
            h,
            p,
            rainfall,
            w
        };

        total_count++;

        // --------------------------------------------------------
        // Extreme temperature
        // --------------------------------------------------------

        if (curr.temperature >= 40.0 ||
            curr.temperature <= 0.0) {

            extreme_temp_events++;
        }

        // --------------------------------------------------------
        // Global sums
        // --------------------------------------------------------

        sum_temp += curr.temperature;
        sum_humidity += curr.humidity;
        sum_pressure += curr.pressure;

        // IMPORTANT:
        // Keep rainfall as double to match MPI implementation.
        sum_rainfall += curr.rainfall;

        sum_wind += curr.wind_speed;

        // --------------------------------------------------------
        // Min / Max
        // --------------------------------------------------------

        if (curr.temperature < min_temp)
            min_temp = curr.temperature;

        if (curr.temperature > max_temp)
            max_temp = curr.temperature;

        if (curr.humidity < min_humidity)
            min_humidity = curr.humidity;

        if (curr.humidity > max_humidity)
            max_humidity = curr.humidity;

        if (curr.pressure < min_pressure)
            min_pressure = curr.pressure;

        if (curr.pressure > max_pressure)
            max_pressure = curr.pressure;

        if (curr.rainfall > max_rainfall)
            max_rainfall = curr.rainfall;

        if (curr.wind_speed > max_wind)
            max_wind = curr.wind_speed;

        // --------------------------------------------------------
        // Hottest / Coldest measurement
        // --------------------------------------------------------

        if (!got_first) {

            hottest = curr;
            coldest = curr;
            got_first = true;

        } else {

            bool is_hottest =
                (curr.temperature != hottest.temperature) ?
                    (curr.temperature > hottest.temperature) :
                ((curr.timestamp != hottest.timestamp) ?
                    (curr.timestamp < hottest.timestamp) :
                    (curr.station_id < hottest.station_id));

            if (is_hottest)
                hottest = curr;

            bool is_coldest =
                (curr.temperature != coldest.temperature) ?
                    (curr.temperature < coldest.temperature) :
                ((curr.timestamp != coldest.timestamp) ?
                    (curr.timestamp < coldest.timestamp) :
                    (curr.station_id < coldest.station_id));

            if (is_coldest)
                coldest = curr;
        }

        // --------------------------------------------------------
        // Station statistics
        // --------------------------------------------------------

        station_map[curr.station_id].count++;

        station_map[curr.station_id].sum_temperature
            += curr.temperature;

        // Keep rainfall as double to match MPI.
        station_map[curr.station_id].sum_rainfall
            += curr.rainfall;

        // --------------------------------------------------------
        // 60-second interval
        // --------------------------------------------------------

        interval_map[curr.timestamp / 60]++;
    }

    fclose(fp);

    if (total_count == 0)
        return 0;

    // ------------------------------------------------------------
    // Find busiest interval
    // ------------------------------------------------------------

    long long busiest_interval = -1;
    long long busiest_count = -1;

    for (auto& entry : interval_map) {

        if (entry.second > busiest_count ||
            (entry.second == busiest_count &&
             (busiest_interval == -1 ||
              entry.first < busiest_interval))) {

            busiest_interval = entry.first;
            busiest_count = entry.second;
        }
    }

    // ------------------------------------------------------------
    // Find Top-K stations
    // ------------------------------------------------------------

    priority_queue<
        StationSummary,
        vector<StationSummary>,
        CompareTopStations
    > pq;

    for (auto& entry : station_map) {

        pq.push({
            entry.first,
            entry.second.count,
            entry.second.sum_temperature,
            entry.second.sum_rainfall
        });

        if ((int)pq.size() > top_k)
            pq.pop();
    }

    vector<StationSummary> final_top_k;

    while (!pq.empty()) {

        final_top_k.push_back(pq.top());
        pq.pop();
    }

    sort(
        final_top_k.begin(),
        final_top_k.end(),
        [](const StationSummary& a,
           const StationSummary& b) {

            if (a.count != b.count)
                return a.count > b.count;

            return a.station_id < b.station_id;
        }
    );

    // ------------------------------------------------------------
    // Output
    // ------------------------------------------------------------

    printf("TOTAL_MEASUREMENTS %d\n",
           total_measurements);

    printf("AVERAGE_TEMPERATURE %.6f\n",
           sum_temp / total_measurements);

    printf("MIN_TEMPERATURE %.6f\n",
           min_temp);

    printf("MAX_TEMPERATURE %.6f\n",
           max_temp);

    printf("AVERAGE_HUMIDITY %.6f\n",
           sum_humidity / total_measurements);

    printf("MIN_HUMIDITY %.6f\n",
           min_humidity);

    printf("MAX_HUMIDITY %.6f\n",
           max_humidity);

    printf("AVERAGE_PRESSURE %.6f\n",
           sum_pressure / total_measurements);

    printf("MIN_PRESSURE %.6f\n",
           min_pressure);

    printf("MAX_PRESSURE %.6f\n",
           max_pressure);

    // Same as MPI: directly print double rainfall sum.
    printf("TOTAL_RAINFALL %.6f\n",
           sum_rainfall);

    printf("MAX_RAINFALL %.6f\n",
           max_rainfall);

    printf("AVERAGE_WIND_SPEED %.6f\n",
           sum_wind / total_measurements);

    printf("MAX_WIND_SPEED %.6f\n",
           max_wind);

    printf("EXTREME_TEMPERATURE_EVENTS %lld\n",
           extreme_temp_events);

    printf("HOTTEST_MEASUREMENT %.6f %lld %lld\n",
           hottest.temperature,
           hottest.station_id,
           hottest.timestamp);

    printf("COLDEST_MEASUREMENT %.6f %lld %lld\n",
           coldest.temperature,
           coldest.station_id,
           coldest.timestamp);

    printf("BUSIEST_INTERVAL %lld %lld\n",
           busiest_interval,
           busiest_count);

    printf("TOP_STATIONS\n");

    for (auto& station : final_top_k) {

        printf(
            "%lld %lld %.6f %.6f\n",
            station.station_id,
            station.count,
            station.sum_temperature /
                station.count,
            station.sum_rainfall
        );
    }

    return 0;
}