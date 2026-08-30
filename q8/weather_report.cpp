#include <cstddef>
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <string>
#include <vector>

using namespace std;

struct Measurement {
    long long timestamp;
    long long stationId;
    double temperature;
    double humidity;
    double pressure;
    double rainfall;
    double windSpeed;
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    int N, K, S;
    vector<Measurement> data;

    // Rank 0 reads the whole input file.
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
                  >> data[i].temperature
                  >> data[i].humidity
                  >> data[i].pressure
                  >> data[i].rainfall
                  >> data[i].windSpeed;
        }
    }

    // Every rank needs the header values for later computation.
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
        for (int r = 0; r < P; ++r) {
            counts[r] = baseCount + (r < remainder ? 1 : 0);
        }

        displacements.resize(P);
        for (int r = 1; r < P; ++r) {
            displacements[r] = displacements[r - 1] + counts[r - 1];
        }
    }

    vector<Measurement> localData(localCount);

    // Describe one Measurement struct to MPI so Scatterv can use record counts,
    // rather than byte counts.
    MPI_Datatype rawMeasurementType, measurementType;
    int blockLengths[] = {1, 1, 5};
    MPI_Aint offsets[] = {
        offsetof(Measurement, timestamp),
        offsetof(Measurement, stationId),
        offsetof(Measurement, temperature)
    };
    MPI_Datatype fieldTypes[] = {MPI_LONG_LONG, MPI_LONG_LONG, MPI_DOUBLE};

    MPI_Type_create_struct(3, blockLengths, offsets, fieldTypes, &rawMeasurementType);
    MPI_Type_create_resized(
        rawMeasurementType, 0, sizeof(Measurement), &measurementType
    );
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

    MPI_Type_free(&measurementType);

    // Each rank now has localData and can calculate its local statistics.

    // TO DO - simple computation and map then reduce and merge maps 

    MPI_Finalize();
    return 0;
}
