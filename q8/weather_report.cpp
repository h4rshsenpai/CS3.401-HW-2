#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <mpi.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iomanip>

using ll = long long;

struct Measurement {
    ll timestamp;
    ll stationId;
    double temp;
    double humidity;
    double pressure;
    double rainfall;
    double windSpeed;
};

struct WeatherTotal {
    double tempSum = 0.0;
    double humidSum = 0.0;
    double pressureSum = 0.0;
    double rainfallSum = 0.0;
    double windSum = 0.0;

    double tempMax = -std::numeric_limits<double>::infinity();
    double pressureMax = -std::numeric_limits<double>::infinity();
    double humidMax = -std::numeric_limits<double>::infinity();
    double rainfallMax = -std::numeric_limits<double>::infinity();
    double windMax = -std::numeric_limits<double>::infinity();

    double tempMin = std::numeric_limits<double>::infinity();
    double pressureMin = std::numeric_limits<double>::infinity();
    double humidMin = std::numeric_limits<double>::infinity();

    ll extreme_temp_count = 0;
    ll count = 0;
};

struct stationStats {
    ll count = 0;
    double tempSum = 0.0;
    double rainSum = 0.0;
};

static inline void update_scalars(const Measurement& M, WeatherTotal& W) {
    ++W.count;

    if (M.temp >= 40.0 || M.temp <= 0.0)
        ++W.extreme_temp_count;

    W.tempSum += M.temp;
    W.humidSum += M.humidity;
    W.pressureSum += M.pressure;
    W.rainfallSum += M.rainfall;
    W.windSum += M.windSpeed;

    W.tempMax = std::max(W.tempMax, M.temp);
    W.humidMax = std::max(W.humidMax, M.humidity);
    W.pressureMax = std::max(W.pressureMax, M.pressure);
    W.rainfallMax = std::max(W.rainfallMax, M.rainfall);
    W.windMax = std::max(W.windMax, M.windSpeed);

    W.tempMin = std::min(W.tempMin, M.temp);
    W.humidMin = std::min(W.humidMin, M.humidity);
    W.pressureMin = std::min(W.pressureMin, M.pressure);
}

static inline bool betterHot(const Measurement& cur, const Measurement& cand) {
    if (cand.temp != cur.temp) return cand.temp > cur.temp;
    if (cand.timestamp != cur.timestamp) return cand.timestamp < cur.timestamp;
    return cand.stationId < cur.stationId;
}

static inline bool betterCold(const Measurement& cur, const Measurement& cand) {
    if (cand.temp != cur.temp) return cand.temp < cur.temp;
    if (cand.timestamp != cur.timestamp) return cand.timestamp < cur.timestamp;
    return cand.stationId < cur.stationId;
}

/* ------------------------ Fast text parsing ----------------------------- */

static inline void skip_spaces(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
}

static inline bool parse_ll(const char*& p, const char* end, ll& value) {
    skip_spaces(p, end);
    if (p >= end) return false;

    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    }

    if (p >= end || !std::isdigit(static_cast<unsigned char>(*p)))
        return false;

    ll x = 0;
    while (p < end && std::isdigit(static_cast<unsigned char>(*p))) {
        x = x * 10 + (*p - '0');
        ++p;
    }

    value = neg ? -x : x;
    return true;
}

static inline bool parse_double(const char*& p, const char* end, double& value) {
    skip_spaces(p, end);
    if (p >= end) return false;

    char* next = nullptr;
    value = std::strtod(p, &next);

    if (next == p || next > end)
        return false;

    p = next;
    return true;
}

static inline bool parse_measurement(const char*& p,
                                     const char* end,
                                     Measurement& m) {
    return parse_ll(p, end, m.timestamp) &&
           parse_ll(p, end, m.stationId) &&
           parse_double(p, end, m.temp) &&
           parse_double(p, end, m.humidity) &&
           parse_double(p, end, m.pressure) &&
           parse_double(p, end, m.rainfall) &&
           parse_double(p, end, m.windSpeed);
}


static MPI_Offset find_header_end(MPI_File file, MPI_Offset fileSize) {
    const int probeSize = 4096;
    std::vector<char> buf(probeSize);

    MPI_Status status;
    int readCount = static_cast<int>(
        std::min<MPI_Offset>(fileSize, probeSize)
    );

    if (readCount <= 0)
        return -1;

    MPI_File_read_at(file, 0, buf.data(), readCount, MPI_BYTE, &status);

    for (int i = 0; i < readCount; ++i) {
        if (buf[i] == '\n') {
            return i + 1;
        }
    }

    return -1;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0, P = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    const std::string filename = (argc > 1) ? argv[1] : "data.txt";

    int N = 0, K = 0, S = 0;

    /*
     * part 1: reading
     * we only need n,k and s and do not required to read all the measurements here.
     */
    if (rank == 0) {
        std::ifstream input(filename);

        if (!input) {
            std::cerr << "Could not open " << filename << '\n';
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        input >> N >> K >> S;
    }

    MPI_Bcast(&N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&S, 1, MPI_INT, 0, MPI_COMM_WORLD);

   /*
 * part 2 - file reading
 * 
 * the naive way: rank 0 reads every single record into memory, then blasts them out to workers using mpi_scatterv.
 * the optimized way: every rank opens the file using mpi-io and reads its own specific chunk of bytes directly from the disk.
 */
    

    MPI_File file;
    if (MPI_File_open(MPI_COMM_WORLD,
                      filename.c_str(),
                      MPI_MODE_RDONLY,
                      MPI_INFO_NULL,
                      &file) != MPI_SUCCESS) {
        if (rank == 0)
            std::cerr << "MPI_File_open failed\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Offset fileSize = 0;
    MPI_File_get_size(file, &fileSize);

    MPI_Offset headerEnd = find_header_end(file, fileSize);

    if (headerEnd < 0) {
        if (rank == 0)
            std::cerr << "Could not find the end of the header.\n";
        MPI_File_close(&file);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    const MPI_Offset dataStart = headerEnd;
    const MPI_Offset dataBytes = fileSize - dataStart;

   
    MPI_Offset roughStart =
        dataStart + (dataBytes * rank) / P;

    MPI_Offset roughEnd =
        (rank == P - 1)
        ? fileSize
        : dataStart + (dataBytes * (rank + 1)) / P;

    MPI_Offset start = roughStart;

    if (rank != 0) {
        char c;
        MPI_Status status;

        while (start < fileSize) {
            MPI_File_read_at(file, start, &c, 1, MPI_BYTE, &status);
            ++start;

            if (c == '\n')
                break;
        }
    }

    
    MPI_Offset end = roughEnd;

    if (rank != P - 1) {
        char c;
        MPI_Status status;

        while (end < fileSize) {
            MPI_File_read_at(file, end, &c, 1, MPI_BYTE, &status);
            ++end;

            if (c == '\n')
                break;
        }
    } else {
        end = fileSize;
    }

    if (end < start)
        end = start;

    const MPI_Offset localBytes64 = end - start;

    
    if (localBytes64 > static_cast<MPI_Offset>(
            std::numeric_limits<size_t>::max())) {
        if (rank == 0)
            std::cerr << "Local chunk is too large for size_t.\n";

        MPI_File_close(&file);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    const size_t localBytes = static_cast<size_t>(localBytes64);
    std::vector<char> buffer(localBytes);

    
    if (localBytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (rank == 0)
            std::cerr << "Chunk exceeds traditional MPI count limit. "
                         "Use chunked MPI-IO for extremely large files.\n";

        MPI_File_close(&file);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Status status;
    if (localBytes > 0) {
        MPI_File_read_at_all(
            file,
            start,
            buffer.data(),
            static_cast<int>(localBytes),
            MPI_BYTE,
            &status
        );
    }

    MPI_File_close(&file);

   /*
 * part 3 - local computations
 * 
 * we just do a single pass over our assigned chunk of text.
 * we intentionally don't create a big vector to hold all the local data.
 * instead, each measurement is parsed, used to update our stats, and immediately thrown away.
 * this shrinks our memory usage way down from o(n) to basically just the size of the text buffer plus the unique stations and time intervals we find.
 */

    WeatherTotal W;

    Measurement localHottest{};
    Measurement localColdest{};
    bool haveExtreme = false;

   
    std::unordered_map<ll, stationStats> localStations;
    std::unordered_map<ll, ll> localIntervals;

    
    if (S > 0) {
        size_t estimatedLocalStations =
            static_cast<size_t>(S / P) + 16;
        localStations.reserve(estimatedLocalStations);
    }

    localIntervals.reserve(1 << 14);

    const char* p = buffer.data();
    const char* endPtr = buffer.data() + buffer.size();

    while (p < endPtr) {
       
        while (p < endPtr &&
               (*p == '\n' || *p == '\r' ||
                *p == ' '  || *p == '\t')) {
            ++p;
        }

        if (p >= endPtr)
            break;

        const char* recordStart = p;
        Measurement M;

        
        if (!parse_measurement(p, endPtr, M)) {
            p = recordStart;
            while (p < endPtr && *p != '\n')
                ++p;
            if (p < endPtr)
                ++p;
            continue;
        }

        update_scalars(M, W);

        
        if (!haveExtreme) {
            localHottest = M;
            localColdest = M;
            haveExtreme = true;
        } else {
            if (betterHot(localHottest, M))
                localHottest = M;

            if (betterCold(localColdest, M))
                localColdest = M;
        }

        
        auto& st = localStations[M.stationId];
        ++st.count;
        st.tempSum += M.temp;
        st.rainSum += M.rainfall;

        
        const ll intervalId = M.timestamp / 60;
        ++localIntervals[intervalId];

        
        while (p < endPtr && *p != '\n')
            ++p;

        if (p < endPtr)
            ++p;
    }

    

    double localSums[5] = {
        W.tempSum,
        W.humidSum,
        W.pressureSum,
        W.rainfallSum,
        W.windSum
    };

    double globalSums[5] = {};

    double localMaxes[5] = {
        W.tempMax,
        W.humidMax,
        W.pressureMax,
        W.rainfallMax,
        W.windMax
    };

    double globalMaxes[5] = {};

    double localMins[3] = {
        W.tempMin,
        W.humidMin,
        W.pressureMin
    };

    double globalMins[3] = {};

    ll globalExtreme = 0;

    MPI_Reduce(localSums, globalSums, 5,
               MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Reduce(localMaxes, globalMaxes, 5,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    MPI_Reduce(localMins, globalMins, 3,
               MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

    MPI_Reduce(&W.extreme_temp_count, &globalExtreme, 1,
               MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    /*
     * Hottest / coldest candidates.
     * Only P records are communicated.
     */
    MPI_Datatype measurementType;

    int blockLengths[3] = {1, 1, 5};
    MPI_Datatype fieldTypes[3] = {
        MPI_LONG_LONG,
        MPI_LONG_LONG,
        MPI_DOUBLE
    };

    MPI_Aint offsets[3] = {
        offsetof(Measurement, timestamp),
        offsetof(Measurement, stationId),
        offsetof(Measurement, temp)
    };

    MPI_Datatype rawMeasurementType;

    MPI_Type_create_struct(
        3,
        blockLengths,
        offsets,
        fieldTypes,
        &rawMeasurementType
    );

    MPI_Type_create_resized(
        rawMeasurementType,
        0,
        sizeof(Measurement),
        &measurementType
    );

    MPI_Type_commit(&measurementType);
    MPI_Type_free(&rawMeasurementType);

    /*
     * Empty ranks are possible when P > N.
     * Give them invalid extreme sentinels.
     */
    Measurement hotSend{};
    Measurement coldSend{};

    hotSend.temp = -std::numeric_limits<double>::infinity();
    coldSend.temp = std::numeric_limits<double>::infinity();

    if (haveExtreme) {
        hotSend = localHottest;
        coldSend = localColdest;
    }

    std::vector<Measurement> hottestCandidates;
    std::vector<Measurement> coldestCandidates;

    if (rank == 0) {
        hottestCandidates.resize(P);
        coldestCandidates.resize(P);
    }

    MPI_Gather(
        &hotSend, 1, measurementType,
        rank == 0 ? hottestCandidates.data() : nullptr,
        1, measurementType,
        0, MPI_COMM_WORLD
    );

    MPI_Gather(
        &coldSend, 1, measurementType,
        rank == 0 ? coldestCandidates.data() : nullptr,
        1, measurementType,
        0, MPI_COMM_WORLD
    );

    MPI_Type_free(&measurementType);

    

    if (rank == 0) {
        // Temporary visibility for Parts 1-4 testing.
        std::cout << "PART_1_3_COMPLETE\n";
        std::cout << "N " << N << "\n";
        std::cout << "K " << K << "\n";
        std::cout << "S " << S << "\n";
        std::cout << "GLOBAL_COUNT " << N << "\n";
        std::cout << "GLOBAL_EXTREME_COUNT " << globalExtreme << "\n";

        if (!hottestCandidates.empty()) {
            Measurement gh = hottestCandidates[0];
            Measurement gc = coldestCandidates[0];

            for (int i = 1; i < P; ++i) {
                if (betterHot(gh, hottestCandidates[i]))
                    gh = hottestCandidates[i];

                if (betterCold(gc, coldestCandidates[i]))
                    gc = coldestCandidates[i];
            }

            std::cout << "HOTTEST "
                      << gh.temp << " "
                      << gh.stationId << " "
                      << gh.timestamp << "\n";

            std::cout << "COLDEST "
                      << gc.temp << " "
                      << gc.stationId << " "
                      << gc.timestamp << "\n";
        }
    }

    MPI_Finalize();
    return 0;
}
