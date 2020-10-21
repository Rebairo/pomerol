#include <Misc.h>
#include <Lattice.h>
#include <LatticePresets.h>
#include <Index.h>
#include <IndexClassification.h>
#include <Operator.h>
#include <OperatorPresets.h>
#include <IndexHamiltonian.h>
#include <Symmetrizer.h>
#include <StatesClassification.h>
#include <HamiltonianPart.h>
#include <Hamiltonian.h>
#include <FieldOperatorContainer.h>
#include <GFContainer.h>

using namespace Pomerol;

RealType beta = 20;
RealType U = 3.7;
RealType mu = U/2*0;
RealType h = 0.0;
RealType V = 1.0;
RealType epsilon = 2.3;

int main(int argc, char* argv[])
{
    MPI_Init(&argc, &argv);

    Lattice L;
    // Correlated site
    L.addSite(new Lattice::Site("C",1,2));
    // Bath sites
    L.addSite(new Lattice::Site("0",1,2));
    //L.addSite(new Lattice::Site("1",1,2));

    LatticePresets::addCoulombS(&L, "C", U, -mu);
/*
    LatticePresets::addMagnetization(&L, "C", 2*h);
    LatticePresets::addLevel(&L, "0", -epsilon);
    LatticePresets::addLevel(&L, "1", epsilon);
    LatticePresets::addHopping(&L, "C", "0", V);
    LatticePresets::addHopping(&L, "C", "1", V);
*/
    IndexClassification IndexInfo(L.getSiteMap());
    IndexInfo.prepare();
    INFO("Indices");
    IndexInfo.printIndices();

    IndexHamiltonian HStorage(&L,IndexInfo);
    HStorage.prepare();

    Symmetrizer Symm(IndexInfo, HStorage);
    //Symm.compute(true);
    Symm.compute(false);

    StatesClassification S(IndexInfo,Symm);
    S.compute();

    Hamiltonian H(IndexInfo, HStorage, S);
    H.prepare();
    for (BlockNumber i=0; i<S.NumberOfBlocks(); i++) {
        INFO(S.getQuantumNumbers(i));
        std::vector<FockState> st = S.getFockStates(i);
        for (int i=0; i<st.size(); ++i) INFO(st[i]);
        INFO("");
    };

    H.compute(MPI_COMM_WORLD);

    DensityMatrix rho(S,H,beta);
    rho.prepare();
    rho.compute();

    FieldOperatorContainer Operators(IndexInfo, S, H);
    Operators.prepareAll();
    Operators.computeAll();

    ParticleIndex down_index = IndexInfo.getIndex("C",0,down);
    ParticleIndex up_index = IndexInfo.getIndex("C",0,up);

    DEBUG(down_index);
    DEBUG(up_index);

    IndexInfo.printIndices();
    for (ParticleIndex i=0; i<IndexInfo.getIndexSize(); i++) {
        INFO("C^+_"<<i);
    }

    GreensFunction GF_down(S,H,
        Operators.getAnnihilationOperator(down_index),
        Operators.getCreationOperator(down_index),
    rho);

    GreensFunction GF_up(S,H,
        Operators.getAnnihilationOperator(up_index),
        Operators.getCreationOperator(up_index),
    rho);

    GF_down.prepare(); DEBUG(""); GF_up.prepare();
    GF_down.compute(); DEBUG(""); GF_up.compute();

    for (size_t i=0; i<10; i++) {
        INFO(GF_down(i) << " " << GF_up(i));
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
