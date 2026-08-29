#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <mpi.h>

using namespace std;

int compute_log(int x) {
    int i = 0;
    while (x > 1) {
        x /= 2;
        i++;
    }
    return i;
}

int comp_asc(const void *a, const void *b) {
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    
    return (val_a > val_b) - (val_a < val_b);
}

int comp_desc(const void *a, const void *b) {
    int val_a = *(const int*)a;
    int val_b = *(const int*)b;

    return (val_b > val_a) - (val_b < val_a);
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    
    int rank, P;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &P);

    if (argc < 2 || argc > 4) {
        if (rank == 0)
            cerr << "usage: " << argv[0] << " N [seed] [--benchmark]\n";
        MPI_Finalize();
        return 1;
    }
    bool benchmark_mode = (argc == 4 && string(argv[3]) == "--benchmark");
    
    if (argc == 4 && !benchmark_mode) {
        cerr << "usage: " << argv[0] << " N [seed] [--benchmark]\n";
        MPI_Finalize();
        return 1;
    }

    int seed = (argc > 2) ? atoi(argv[2]) : 24;
    int  N = atoi(argv[1]);

    vector<int> global_arr;

    if (rank == 0) {
        srand(seed);
        global_arr.resize(N);
        for (int i = 0; i < N; i++)
            global_arr[i] = rand() % 1000000;
        
        // outputs generated array for correctness check
        if (!benchmark_mode) {  
            for (auto x: global_arr) cout << x << " ";
            cout << "\n";
        }
    }

    int local_n = N/P;
    vector<int> local_arr(local_n);
    
    // ------- start timer --------

    double t_start = MPI_Wtime(); 
    MPI_Scatter(global_arr.data(), local_n, MPI_INT, local_arr.data(), local_n, MPI_INT, 0, MPI_COMM_WORLD);
    
    // each node sorts its own local chunk
    qsort(local_arr.data(), local_n, sizeof(int), ((rank + 1) % 2) ? comp_asc : comp_desc);

    vector<int> recv_arr(local_n); 

    for (int i = 0; i < compute_log(P); i++) {
        bool ascending = ((rank >> (i+1)) & 1) == 0;

        for(int j = i; j >= 0; j--) {
            
            int partner = rank ^ (1 << j);
            bool lower = (rank < partner) == 1;
            
            MPI_Sendrecv(local_arr.data(), local_n, MPI_INT, partner, 0, recv_arr.data(), local_n, MPI_INT, partner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int temp;
            for (int k = 0; k < local_n; k++) {

                if (local_arr[k] > recv_arr[k])
                    temp = (lower == ascending) ? recv_arr[k] : local_arr[k];
                else 
                    temp = (lower == ascending) ? local_arr[k] : recv_arr[k];
            
                local_arr[k] = temp;                
            } 
        }
        qsort(local_arr.data(), local_n, sizeof(int), ascending ? comp_asc : comp_desc);
    }

    vector<int> sorted_arr(N);
    MPI_Gather(local_arr.data(), local_n, MPI_INT, sorted_arr.data(), local_n, MPI_INT, 0, MPI_COMM_WORLD);

// --------- end timer ------------------
    double t_end = MPI_Wtime();

    // outputs sorted array for correctness check 
    if (rank == 0) {
        if (!benchmark_mode) {
            for (auto x : sorted_arr) cout << x << " ";
            cout << "\n";
        }
        cout << t_end - t_start << "\n";
    }
    
    MPI_Finalize();
    return 0;
}
