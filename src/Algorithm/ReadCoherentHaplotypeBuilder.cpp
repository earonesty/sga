///----------------------------------------------
// Copyright 2011 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// ReadCoherentHaplotypeBuilder - Construct candidate
// haplotypes from a pair of k-mer seeds.
//
#include "ReadCoherentHaplotypeBuilder.h"
#include "BWTAlgorithms.h"
#include "SGSearch.h"
#include "SGAlgorithms.h"
#include "Profiler.h"
#include "HapgenUtil.h"
#include "multiple_alignment.h"

//
//
//
ReadCoherentHaplotypeBuilder::ReadCoherentHaplotypeBuilder() : m_pBWT(NULL), m_kmer(0), m_maxEditDistance(1)
{
}

//
ReadCoherentHaplotypeBuilder::~ReadCoherentHaplotypeBuilder()
{
}

//
void ReadCoherentHaplotypeBuilder::setKmer(size_t k)
{
    m_kmer = k;
    m_kmerThreshold = 1;
}

// The source string is the string the bubble starts from
void ReadCoherentHaplotypeBuilder::setInitialHaplotype(const std::string& sequence)
{
    printf("Starting new haplotype %s\n", sequence.c_str());
    m_haplotypes.push_back(sequence);
}

// The source index is the index that the contains the source string
void ReadCoherentHaplotypeBuilder::setIndex(const BWT* pBWT, const BWTIntervalCache* pCache, 
                                            const SampledSuffixArray* pSSA)
{
    m_pBWT = pBWT;
    m_pIntervalCache = pCache;
    m_pSSA = pSSA;
}

// Run the bubble construction process
HaplotypeBuilderReturnCode ReadCoherentHaplotypeBuilder::run(StringVector& out_haplotypes)
{
    PROFILE_FUNC("ReadCoherentHaplotypeBuilder::run")
    assert(m_haplotypes.size() == 1);
    assert(m_pBWT != NULL);

    int round = 0;
    int MAX_ROUNDS = 100;

    // Extend each haplotype a single base
    while(round++ < MAX_ROUNDS)
    {
        printf("Round %d -- haplotypes: %zu\n", round, m_haplotypes.size());
        extendOnce(ED_SENSE);
        extendOnce(ED_ANTISENSE);

        // Every x rounds cull incoherent haplotypes
        if(round % 10 == 0)
            cullHaplotypes();
    }

    printf("Final haplotypes: %zu\n", m_haplotypes.size());
    out_haplotypes.clear();
    out_haplotypes.insert(out_haplotypes.end(), m_haplotypes.begin(), m_haplotypes.end());

    return HBRC_OK;
}

void ReadCoherentHaplotypeBuilder::extendOnce(EdgeDir direction)
{
    // A list to add new haplotypes to
    std::list<std::string> incoming_haplotypes;
    
    // Extend each haplotype, possibly branching the search
    for(std::list<std::string>::iterator iterator = m_haplotypes.begin(); 
                                         iterator != m_haplotypes.end(); ++iterator)
    {
        std::string query_kmer;
        if(direction == ED_SENSE)
            query_kmer = iterator->substr(iterator->length() - m_kmer);
        else
            query_kmer = iterator->substr(0, m_kmer);

        // Get the valid extensions of this sequence
        AlphaCount64 extensionCounts = BWTAlgorithms::calculateDeBruijnExtensionsSingleIndex(query_kmer, m_pBWT, direction);
        std::string extensionBases;
        for(size_t i = 0; i < DNA_ALPHABET::size; ++i)
        {
            char b = DNA_ALPHABET::getBase(i);
            size_t count = extensionCounts.get(b);
            if(count >= m_kmerThreshold)
                extensionBases.push_back(b);
        }

        // Extend the haplotype, or create new haplotypes if there is a branch
        if(extensionBases.length() == 1)
        {
            if(direction == ED_SENSE)
                iterator->append(1,extensionBases[0]);
            else
                iterator->insert(0, 1, extensionBases[0]); // slow
        }
        else if(extensionBases.length() > 1) // branch
        {
            // Branch the current haplotype for all the extensions but one
            for(size_t i = 1; i < extensionBases.length(); ++i)
            {
                std::string branched = *iterator;
                if(direction == ED_SENSE)
                    branched.append(1,extensionBases[i]);
                else
                    branched.insert(0, 1, extensionBases[i]); // slow
                incoming_haplotypes.push_back(branched);   
            }

            // ...and update the iterator for the remaining extension
            if(direction == ED_SENSE)
                iterator->append(1,extensionBases[0]);
            else
                iterator->insert(0, 1, extensionBases[0]); // slow

        }
    }

    // Merge the incoming haplotypes
    m_haplotypes.splice(m_haplotypes.end(), incoming_haplotypes, incoming_haplotypes.begin(), incoming_haplotypes.end());
}

//
void ReadCoherentHaplotypeBuilder::cullHaplotypes()
{
    m_reads.clear();
    getReads();
    std::list<std::string>::iterator iterator = m_haplotypes.begin();
    while(iterator != m_haplotypes.end())
    {
        int max_jump = calculateHaplotypeIncoherency(*iterator);
        if(max_jump > 20)
            iterator = m_haplotypes.erase(iterator);
        else
            iterator++;
    }
}


//
void ReadCoherentHaplotypeBuilder::getReads()
{
    SeqItemVector variant_reads;
    SeqItemVector variant_rc_reads;

    std::vector<std::string> haplotype_vector(m_haplotypes.begin(), m_haplotypes.end());

    // Forward reads
    HapgenUtil::extractHaplotypeReads(haplotype_vector, m_pBWT, m_pIntervalCache, m_pSSA, 
                                      m_kmer, false, 100000, &variant_reads, NULL);

    // Reverse reads
    HapgenUtil::extractHaplotypeReads(haplotype_vector, m_pBWT, m_pIntervalCache, m_pSSA, 
                                      m_kmer, true, 100000, &variant_rc_reads, NULL);

    // Move read sequences to the vector of reads
    for(size_t i = 0; i < variant_reads.size(); ++i)
        m_reads.push_back(variant_reads[i].seq.toString());

    for(size_t i = 0; i < variant_rc_reads.size(); ++i)
        m_reads.push_back(reverseComplement(variant_rc_reads[i].seq.toString()));
}

//
int ReadCoherentHaplotypeBuilder::calculateHaplotypeIncoherency(const std::string& haplotype)
{
    MultipleAlignment multiple_alignment;
    multiple_alignment.addBaseSequence("haplotype", haplotype, "");
        
    // A vector to record the start position of each read mapped to this haplotype
    std::vector<int> start_vector; 

    for(size_t i = 0; i < m_reads.size(); ++i)
    {
        const std::string& read = m_reads[i];

        // Try to align this string using mismatches only at every start position of the haplotype
        // Awful algorithm, to be replaced later
        size_t best_mismatches = std::numeric_limits<size_t>::max();
        std::vector<size_t> best_starts;
        for(size_t j = 0; j < haplotype.size(); ++j)
        {
            size_t mismatches = 0;
            for(size_t k = 0; k < read.size(); ++k)
                mismatches += (j + k < haplotype.size() && haplotype[j + k] == read[k] ? 0 : 1);

            if(mismatches < best_mismatches)
            {
                best_mismatches = mismatches;
                best_starts.clear();
                best_starts.push_back(j);
            }
            else if(mismatches == best_mismatches)
            {
                best_starts.push_back(j);
            }
        }

        for(size_t j = 0; j < best_starts.size(); ++j)
        {
            // Craft a sequence overlap object for each best match
            SequenceOverlap overlap = Overlapper::extendMatch(haplotype, read, best_starts[j], 0, 2);

            if(overlap.edit_distance <= (int)m_maxEditDistance)
            {
//                printf("Best match of read[%zu] on haplotype: %zu mismatches, position %zu ma: %zu\n", i, best_mismatches, best_starts[j], best_starts.size());
                // Calculate a proper overlap between the sequences and add to the multiple alignment
                multiple_alignment.addOverlap("noname", read, "", overlap);
                start_vector.push_back(overlap.match[0].start);
            }
        }
    }

    // Calculate the maximum distance between consecutive start positions
    std::sort(start_vector.begin(), start_vector.end());
    int max_jump = 0;
    for(size_t i = 0; i < start_vector.size() - 1; ++i)
    {
        int jump = start_vector[i+1] - start_vector[i];
        if(jump > max_jump)
            max_jump = jump;
    }
    return max_jump;
}
