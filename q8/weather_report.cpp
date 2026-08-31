#include <cstddef>
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <string>
#include <vector>
#include <unordered_map>

typedef long long ll;

using namespace std;

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
    double tempSum = 0;
    double humidSum = 0;
    double pressureSum = 0;
    double rainfallSum = 0;
    double windSum = 0;

    double tempMax = -1000;
    double pressureMax = -1000;
    double humidMax = -1000;
    double rainfallMax = -1000;
    double windMax = -1000;

    double tempMin = 1000;
    double pressureMin = 1000;
    double humidMin = 1000;

    ll extreme_temp_count = 0;
    ll count = 0;
};

struct stationStats {
    ll count = 0;
    double tempSum = 0.0;
    double rainSum = 0.0;
};

void update_scalars(Measurement* M, WeatherTotal *W) {
        
    W->count++;
    if (M->temp > 40.0 || M->temp <= 0.0) 
        W->extreme_temp_count++;

    W->tempSum += M->temp;
    W->humidSum += M->humidity;
    W->pressureSum += M->pressure;
    W->rainfallSum += M->rainfall;
    W->windSum += M->windSpeed;

    W->tempMax = (W->tempMax < M->temp)? M->temp : W->tempMax;
    W->humidMax = (W->humidMax < M->humidity)? M->humidity : W->humidMax;
    W->pressureMax = (W->pressureMax < M->pressure)? M->pressure : W->pressureMax;
    W->rainfallMax = (W->rainfallMax < M->rainfall)? M->rainfall : 

    W->tempMin = (W->tempMin > M->temp)? M->temp : W->tempMin;
    W->humidMin = (W->humidMin > M->humidity)? M->humidity : W->humidMin;
    W->pressureMin = (W->pressureMin > M->pressure)? M->pressure : W->pressureMin;

}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    int N, K, S;
    vector<Measurement> data;


    /*
     ----------------------- Part 1: READ -----------------------------
       v1 -  Rank 0 reads the whole input file. 
       v2 - Each rank reads their own separate chunk into RAM - use mmap
    */

    if (rank == 0) {
        const string filename = (argc > 1) ? argv[1] : "data.txt";
        ifstream input(filename);

        if (!input) {
            cerr << "Could not open " << filename << '\n';
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        input >> N >> K >> S;
        data.resize(N);

        for (int i = 0; i < N; ++i) {
            input >> data[i].timestamp
                  >> data[i].stationId
                  >> data[i].temp
                  >> data[i].humidity
                  >> data[i].pressure
                  >> data[i].rainfall
                  >> data[i].windSpeed;
        }
    }

    // Every rank needs N, K and S for local computation.
    MPI_Bcast(&N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&S, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Distribute the remainder evenly: the first `remainder` ranks get one extra record.
    const int baseCount = N / P;
    const int remainder = N % P;
    const int localCount = baseCount + (rank < remainder ? 1 : 0);

    vector<int> counts;
    vector<int> displacements;

    if (rank == 0) {
        counts.resize(P);
        for (int r = 0; r < P; r++) {
            counts[r] = baseCount + (r < remainder ? 1 : 0);
        }

        displacements.resize(P);
        for (int r = 1; r < P; r++) {
            displacements[r] = displacements[r - 1] + counts[r - 1];
        }
    }

    vector<Measurement> localData(localCount);

    // Treat one Measurement struct as one MPI object so Scatterv can use 
    // record counts, rather than raw bytes 
    MPI_Datatype rawMeasurementType, measurementType;
    
    int blockLengths[3] = {1, 1, 5};
    MPI_Datatype fieldTypes[] = {MPI_LONG_LONG, MPI_LONG_LONG, MPI_DOUBLE};

    MPI_Aint offsets[] = {
        offsetof(Measurement, timestamp),
        offsetof(Measurement, stationId),
        offsetof(Measurement, temp)
    };

    MPI_Type_create_struct(3, blockLengths, offsets, fieldTypes, &rawMeasurementType);
    MPI_Type_create_resized(rawMeasurementType, 0, sizeof(Measurement), &measurementType);
    // --> finish with defining Measurement

    MPI_Type_commit(&measurementType);
    MPI_Type_free(&rawMeasurementType);

    MPI_Scatterv(
        rank == 0 ? data.data() : nullptr,
        rank == 0 ? counts.data() : nullptr,
        rank == 0 ? displacements.data() : nullptr,
        measurementType,
        localData.data(), 
        localCount, 
        measurementType,
        0,
        MPI_COMM_WORLD
    );


    /* ----------- PART 2: LOCAL COMPUTATIONS -----------------
        v1 - compute scalars locally and create local maps for busiest interval and top K
            --> reduce scalars and merge maps at rank 0 and compute final results

        v2 - Nodes incharge of separate stations, each computes for their list and
            --> compute scalars grouped by stations, then forward to node according to 
                hashed stationIDs
    */

    WeatherTotal W;
    Measurement localHottest, localColdest;

    // local maps for top K and busiest interval
    unordered_map<long long, stationStats> topK; // stationId -> {count, avg temp, tot rainfall}
    unordered_map<double, long long> busiest;   // interval -> count

    localHottest.temp = -1000; localColdest.temp = 1000;

    for (ll i = 0; i < localCount; i++) {
        
        Measurement *M = &localData[i];
        update_scalars(M, &W);
        
        if (localHottest.temp < M->temp) localHottest.temp = M->temp;
        if (localColdest.temp > M->temp) localColdest.temp = M->temp;
    
        // update top K
        auto& entry = topK[M->stationId]; 
        entry.count++;
        entry.tempSum += M->temp;
        entry.rainSum += M->rainfall;
                   
        // update busiest interval 
        auto& interval = busiest[(double)M->timestamp/60];
        entry.count++;
    }    

    /* 
        ---------- Part 3: Communication -----------
        --> revamp entirely for v2
    */
    
    // Reduce scalars at Rank 0

    double localSums[5] = {W.tempSum, W.humidSum, W.pressureSum, W.rainfallSum, W.windSum};
    double globalSums[5];

    double localMaxes[5] = {W.tempMax, W.humidMax, W.pressureMax, W.rainfallMax, W.windMax};
    double globalMaxes[5];

    double localMins[3] = {W.tempMin, W.humidMin, W.pressureMin};
    double globalMins[3];

    MPI_Reduce(localSums, globalSums, 5, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(localMaxes, globalMaxes, 5, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(localMins, globalMins, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);


    Measurement hottestCandidate[P], coldestCandidate[P];     // each node sends 1 of each
    MPI_Gather(&localHottest, 1, measurementType, hottestCandidate, P, measurementType, 0, MPI_COMM_WORLD);
    MPI_Gather(&localColdest, 1, measurementType, coldestCandidate, P, measurementType, 0, MPI_COMM_WORLD);

    MPI_Type_free(&measurementType);

 
    // compute avg, min, max and total
    double avgs[5];
    for (int i = 0; i < 5; i++)
        avgs[0] = globalSums[0] / N;


    // select hottest and coldest candidate
    Measurement globalHottest, globalColdest;
    globalHottest.temp = -1000;
    globalColdest.temp = 1000;

    for (int i = 0; i < P; i++) {
        // hottest first
        if (globalHottest.temp == hottestCandidate[i].temp) {
            if (globalHottest.timestamp < hottestCandidate[i].timestamp) {
                globalHottest = hottestCandidate[i];
            }
            else { 
                globalHottest = (globalHottest.stationId < hottestCandidate[i].stationId)? globalHottest : hottestCandidate[i];
            }
        }
        else {
            globalHottest = (globalHottest.temp < hottestCandidate[i].temp)? hottestCandidate[i] : globalHottest;
        }
        
        //coldest
        if (globalColdest.temp == coldestCandidate[i].temp) {
            if (globalColdest.timestamp < coldestCandidate[i].timestamp) {
                globalColdest = coldestCandidate[i];
            }
            else { 
                globalColdest = (globalColdest.stationId < coldestCandidate[i].stationId)? globalColdest : coldestCandidate[i];
            }
        }
        else {
            globalColdest = (globalColdest.temp > coldestCandidate[i].temp)? hottestCandidate[i] : globalColdest;
        }

    }
    // mins, maxs already calculated

    
    
    MPI_Finalize();
    return 0;
}
