#include "pomerol/Hamiltonian.h"
#include "mpi_dispatcher/mpi_skel.hpp"

#ifdef ENABLE_SAVE_PLAINTEXT
#include<boost/filesystem.hpp>
#endif

namespace Pomerol{

Hamiltonian::Hamiltonian(const IndexClassification &IndexInfo, const IndexHamiltonian& F, const StatesClassification &S):
    ComputableObject(), IndexInfo(IndexInfo), F(F), S(S)
{}

Hamiltonian::~Hamiltonian()
{
}

void Hamiltonian::prepare(const boost::mpi::communicator& comm)
{
    if (Status >= Prepared) return;
    BlockNumber NumberOfBlocks = S.NumberOfBlocks();
    parts.resize(NumberOfBlocks);
    if (!comm.rank()) INFO_NONEWLINE("Preparing Hamiltonian parts...");


    for (BlockNumber CurrentBlock = 0; CurrentBlock < NumberOfBlocks; CurrentBlock++)
    {
	    parts[CurrentBlock].reset(new HamiltonianPart(IndexInfo,F, S, CurrentBlock));
        //parts[CurrentBlock]->prepare();
    }
    pMPI::mpi_skel<pMPI::PrepareWrap<HamiltonianPart> > skel;
    skel.parts.resize(parts.size());
    for (size_t i=0; i<parts.size(); i++) { skel.parts[i] = pMPI::PrepareWrap<HamiltonianPart>(*parts[i]);};
    std::map<pMPI::JobId, pMPI::WorkerId> job_map = skel.run(comm,false);
    comm.barrier();
    for (size_t p = 0; p<parts.size(); p++) {
            if (comm.rank() == job_map[p]){
                if (parts[p]->Status != HamiltonianPart::Prepared) { 
                    ERROR ("Worker" << comm.rank() << " didn't calculate part" << p); 
                    throw (std::logic_error("Worker didn't calculate this part."));
                    };
                boost::mpi::broadcast(comm, parts[p]->H.data(), parts[p]->H.rows()*parts[p]->H.cols(), comm.rank());
                }
            else {
                parts[p]->H.resize(parts[p]->getSize(),parts[p]->getSize());
                boost::mpi::broadcast(comm, parts[p]->H.data(), parts[p]->getSize()*parts[p]->getSize(), job_map[p]);
                parts[p]->Status = HamiltonianPart::Prepared;
                 };
            };
    Status = Prepared;
}


void Hamiltonian::compute(const boost::mpi::communicator & comm)
{
    if (Status >= Computed) return;

    // Create a "skeleton" class with pointers to part that can call a compute method
    pMPI::mpi_skel<pMPI::ComputeWrap<HamiltonianPart> > skel;
    skel.parts.resize(parts.size());
    for (size_t i=0; i<parts.size(); i++) { skel.parts[i] = pMPI::ComputeWrap<HamiltonianPart>(*parts[i],parts[i]->getSize());};
    std::map<pMPI::JobId, pMPI::WorkerId> job_map = skel.run(comm, true);
    int rank = comm.rank();
    int comm_size = comm.size(); 

    // Start distributing data
    comm.barrier();
    for (size_t p = 0; p<parts.size(); p++) {
            if (rank == job_map[p]){
                if (parts[p]->Status != HamiltonianPart::Computed) { 
                    ERROR ("Worker" << rank << " didn't calculate part" << p); 
                    throw (std::logic_error("Worker didn't calculate this part."));
                    };
                boost::mpi::broadcast(comm, parts[p]->H.data(), parts[p]->H.rows()*parts[p]->H.cols(), rank);
                boost::mpi::broadcast(comm, parts[p]->Eigenvalues.data(), parts[p]->H.rows(), rank);
                }
            else {
                parts[p]->Eigenvalues.resize(parts[p]->H.rows());
                boost::mpi::broadcast(comm, parts[p]->H.data(), parts[p]->H.rows()*parts[p]->H.cols(), job_map[p]);
                boost::mpi::broadcast(comm, parts[p]->Eigenvalues.data(), parts[p]->H.rows(), job_map[p]);
                parts[p]->Status = HamiltonianPart::Computed;
                 };
            };
/*
    for (BlockNumber CurrentBlock=0; CurrentBlock<NumberOfBlocks; CurrentBlock++)
    {
	    parts[CurrentBlock]->compute();
	    INFO("Hpart " << CurrentBlock << " (" << S.getQuantumNumbers(CurrentBlock) << ") is diagonalized.");
    }
*/
    computeGroundEnergy();
    Status = Computed;
}

void Hamiltonian::reduce(const RealType Cutoff)
{
    std::cout << "Performing EV cutoff at " << Cutoff << " level" << std::endl;
    BlockNumber NumberOfBlocks = parts.size();
    for (BlockNumber CurrentBlock=0; CurrentBlock<NumberOfBlocks; CurrentBlock++)
    {
	parts[CurrentBlock]->reduce(GroundEnergy+Cutoff);
    }
}

void Hamiltonian::computeGroundEnergy()
{
    RealVectorType LEV(size_t(S.NumberOfBlocks()));
    BlockNumber NumberOfBlocks = parts.size();
    for (BlockNumber CurrentBlock=0; CurrentBlock<NumberOfBlocks; CurrentBlock++) {
	    LEV(static_cast<int>(CurrentBlock),0) = parts[CurrentBlock]->getMinimumEigenvalue();
    }
    GroundEnergy=LEV.minCoeff();
}

const HamiltonianPart& Hamiltonian::getPart(const QuantumNumbers &in) const
{
    return *parts[S.getBlockNumber(in)];
}

const HamiltonianPart& Hamiltonian::getPart(BlockNumber in) const
{
    return *parts[in];
}

RealType Hamiltonian::getEigenValue(QuantumState state) const
{
    InnerQuantumState InnerState = S.getInnerState(state);
    return getPart(S.getBlockNumber(state)).getEigenValue(InnerState);
}

RealVectorType Hamiltonian::getEigenValues() const
{
    RealVectorType out(S.getNumberOfStates());
    size_t i=0;
    for (BlockNumber CurrentBlock=0; CurrentBlock<S.NumberOfBlocks(); CurrentBlock++) {
        const RealVectorType& tmp = parts[CurrentBlock]->getEigenValues();
        std::copy(tmp.data(), tmp.data() + tmp.size(), out.data()+i);
        i+=tmp.size(); 
        }
    return out;
}


RealType Hamiltonian::getGroundEnergy() const
{
    return GroundEnergy;
}

#ifdef ENABLE_SAVE_PLAINTEXT
bool Hamiltonian::savetxt(const boost::filesystem::path &path)
{
    BlockNumber NumberOfBlocks = parts.size();
    boost::filesystem::create_directory(path);
    for (BlockNumber CurrentBlock=0; CurrentBlock<NumberOfBlocks; CurrentBlock++) {
        std::stringstream tmp;
        tmp << "part" << S.getQuantumNumbers(CurrentBlock);
        boost::filesystem::path out = path / boost::filesystem::path (tmp.str()); 
	    parts[CurrentBlock]->savetxt(out);
        }
    return true;
}
#endif

} // end of namespace Pomerol
