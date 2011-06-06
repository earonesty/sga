//-----------------------------------------------
// Copyright 2011 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// LRCorrection - Collection of algorithms for 
// correcting long reads with a set of short reads
//
#ifndef LRCORRECTION_H
#define LRCORRECTION_H

#include "LRAlignment.h"

namespace LRCorrection
{
    std::string correctLongRead(const std::string& query,
                                const BWT* pTargetBWT, 
                                const SampledSuffixArray* pTargetSSA,
                                const LRAlignment::LRParams& params);

};

#endif
