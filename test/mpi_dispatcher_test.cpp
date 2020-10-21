#include <mpi_dispatcher/misc.hpp>
#include <mpi_dispatcher/mpi_dispatcher.hpp>
#include <thread>
#include <random>
#include <iostream>

using namespace pMPI;

int dumb_task_counter;

void dumb_task(double seconds, int jobid, int rank) {
    std::cout << "[" << rank << "] running job " << jobid << " " << seconds << " seconds..." << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(int(seconds * 1000)));
    ++dumb_task_counter;
    std::cout << "done." << std::endl;
};

int main(int argc, char *argv[]) {

    MPI_Init(&argc, &argv);

    std::random_device rd;
    std::mt19937 gen(100000);
    std::uniform_real_distribution<double> dist(0, 0.1);
    size_t ROOT = 0;
    int rank = pMPI::rank(MPI_COMM_WORLD);

    try {

        MPIWorker worker(MPI_COMM_WORLD, ROOT);
        int ntasks = 45;
        dumb_task_counter = 0;

        std::unique_ptr<MPIMaster> disp;

        if (rank == ROOT) {
            disp.reset(new MPIMaster(MPI_COMM_WORLD, ntasks, true));
            disp->order();
            std::cout << "ordered" << std::endl;
        };
        MPI_Barrier(MPI_COMM_WORLD);

        for (; !worker.is_finished();) {
            if (rank == ROOT) disp->order();
            worker.receive_order();
            if (worker.is_working()) {
                dumb_task(dist(gen), worker.current_job(), rank);
                worker.report_job_done();
            };
            if (rank == ROOT)
                std::cout << "--> stack size = " << disp->JobStack.size()
                          << " --> worker stack size =" << disp->WorkerStack.size()
                          << std::endl << std::flush;
            if (rank == ROOT)
                disp->check_workers();
        };
        if (rank == ROOT) disp.release();

        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Allreduce(MPI_IN_PLACE, &dumb_task_counter, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if(dumb_task_counter != ntasks) {
            std::cout << "ntasks = " << ntasks
                      << ", dumb_task_counter = "
                      << dumb_task_counter << std::endl;
            return EXIT_FAILURE;
        }
    } // end try
    catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    };

    MPI_Finalize();
    return EXIT_SUCCESS;
}
