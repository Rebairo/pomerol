//
// This file is a part of pomerol - a scientific ED code for obtaining
// properties of a Hubbard model on a finite-size lattice
//
// Copyright (C) 2010-2012 Andrey Antipov <Andrey.E.Antipov@gmail.com>
// Copyright (C) 2010-2012 Igor Krivenko <Igor.S.Krivenko@gmail.com>
//
// pomerol is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// pomerol is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with pomerol.  If not, see <http://www.gnu.org/licenses/>.


#include "pomerol/TwoParticleGFContainer.h"
#include <boost/serialization/complex.hpp>
#include <boost/serialization/vector.hpp>

namespace Pomerol{

TwoParticleGFContainer::TwoParticleGFContainer(const IndexClassification& IndexInfo, const StatesClassification &S,
                                               const Hamiltonian &H, const DensityMatrix &DM, const FieldOperatorContainer& Operators) :
    IndexContainer4<TwoParticleGF,TwoParticleGFContainer>(this,IndexInfo), Thermal(DM),
    S(S),H(H),DM(DM), Operators(Operators),
    ReduceResonanceTolerance (1e-8),//1e-16),
    CoefficientTolerance (1e-16),//1e-16),
    MultiTermCoefficientTolerance (1e-5)//1e-5),
{}

void TwoParticleGFContainer::prepareAll(const std::set<IndexCombination4>& InitialIndices)
{
    fill(InitialIndices);
    for(std::map<IndexCombination4,ElementWithPermFreq<TwoParticleGF> >::iterator iter = ElementsMap.begin();
        iter != ElementsMap.end(); iter++) {
        static_cast<TwoParticleGF&>(iter->second).ReduceResonanceTolerance = ReduceResonanceTolerance;
        static_cast<TwoParticleGF&>(iter->second).CoefficientTolerance = CoefficientTolerance;
        static_cast<TwoParticleGF&>(iter->second).MultiTermCoefficientTolerance = MultiTermCoefficientTolerance;
        static_cast<TwoParticleGF&>(iter->second).prepare();
       };
}

std::map<IndexCombination4,std::vector<ComplexType> > TwoParticleGFContainer::computeAll(bool clearTerms, std::vector<boost::tuple<ComplexType, ComplexType, ComplexType> > const& freqs, const boost::mpi::communicator & comm, bool split)
{
    if (split)
        return computeAll_split(clearTerms, freqs, comm);
    else
        return computeAll_nosplit(clearTerms, freqs, comm);
}

std::map<IndexCombination4,std::vector<ComplexType> > TwoParticleGFContainer::computeAll_nosplit(bool clearTerms, std::vector<boost::tuple<ComplexType, ComplexType, ComplexType> > const& freqs, const boost::mpi::communicator & comm)
{
    std::map<IndexCombination4,std::vector<ComplexType> > out;
    for(std::map<IndexCombination4,ElementWithPermFreq<TwoParticleGF> >::iterator iter = ElementsMap.begin();
        iter != ElementsMap.end(); iter++) {
        INFO("Computing 2PGF for " << iter->first);
        out.insert(std::make_pair(iter->first, static_cast<TwoParticleGF&>(iter->second).compute(clearTerms, freqs, comm)));
        };
    return out;
}

std::map<IndexCombination4,std::vector<ComplexType> > TwoParticleGFContainer::computeAll_split(bool clearTerms, std::vector<boost::tuple<ComplexType, ComplexType, ComplexType> > const& freqs, const boost::mpi::communicator & comm)
{
    std::map<IndexCombination4,std::vector<ComplexType> > out;
    std::map<IndexCombination4,std::vector<ComplexType> > storage;
    // split communicator
    size_t ncomponents = NonTrivialElements.size();
    size_t ncolors = std::min(int(comm.size()), int(NonTrivialElements.size()));
    RealType color_size = 1.0*comm.size()/ncolors;
    std::map<int,int> proc_colors;
    std::map<int,int> elem_colors;
    std::map<int,int> color_roots;
    bool calc = false;
    for (size_t p=0; p<comm.size(); p++) {
        int color = int (1.0*p / color_size);
        proc_colors[p] = color;
        color_roots[color]=p;
    }
    for (size_t i=0; i<ncomponents; i++) {
        int color = i*ncolors/ncomponents;
        elem_colors[i] = color;
    };

    if (!comm.rank()) {
        INFO("Splitting " << ncomponents << " components in " << ncolors << " communicators");
        for (size_t i=0; i<ncomponents; i++)
        INFO("2pgf " << i << " color: " << elem_colors[i] << " color_root: " << color_roots[elem_colors[i]]);
        };
    comm.barrier();
    int comp = 0;

    boost::mpi::communicator comm_split = comm.split(proc_colors[comm.rank()]);

    for(std::map<IndexCombination4, std::shared_ptr<TwoParticleGF> >::iterator iter = NonTrivialElements.begin(); iter != NonTrivialElements.end(); iter++, comp++) {
        bool calc = (elem_colors[comp] == proc_colors[comm.rank()]);
        if (calc) {
            INFO("C" << elem_colors[comp] << "p" << comm.rank() << ": computing 2PGF for " << iter->first);
            if (calc) storage[iter->first] = static_cast<TwoParticleGF&>(*(iter->second)).compute(clearTerms, freqs, comm_split);
            };
        };
    comm.barrier();
    // distribute data
    if (!comm.rank()) INFO_NONEWLINE("Distributing 2PGF container...");
    comp = 0;
    for(std::map<IndexCombination4, std::shared_ptr<TwoParticleGF> >::iterator iter = NonTrivialElements.begin(); iter != NonTrivialElements.end(); iter++, comp++) {
        int sender = color_roots[elem_colors[comp]];
        TwoParticleGF& chi = *((iter)->second);
        for (size_t p = 0; p<chi.parts.size(); p++) {
        //    if (comm.rank() == sender) INFO("P" << comm.rank() << " 2pgf " << p << " " << chi.parts[p]->NonResonantTerms.size());
            boost::mpi::broadcast(comm, chi.parts[p]->NonResonantTerms, sender);
            boost::mpi::broadcast(comm, chi.parts[p]->ResonantTerms, sender);
            std::vector<ComplexType> freq_data;
            if (comm.rank() == sender) freq_data = storage[iter->first];
            boost::mpi::broadcast(comm, freq_data, sender);
            out[iter->first] = freq_data;

            if (comm.rank() != sender) {
                chi.setStatus(TwoParticleGF::Computed);
                 };
            };
    }
    comm.barrier();
    if (!comm.rank()) INFO("done.");
    return out;
}

TwoParticleGF* TwoParticleGFContainer::createElement(const IndexCombination4& Indices) const
{
    const AnnihilationOperator &C1 = Operators.getAnnihilationOperator(Indices.Index1);
    const AnnihilationOperator &C2 = Operators.getAnnihilationOperator(Indices.Index2);
    const CreationOperator     &CX3 = Operators.getCreationOperator   (Indices.Index3);
    const CreationOperator     &CX4 = Operators.getCreationOperator   (Indices.Index4);

    return new TwoParticleGF(S,H,C1,C2,CX3,CX4,DM);
}

} // end of namespace Pomerol
