#ifndef NOVINS_CROP_UNMAPPED_H_
#define NOVINS_CROP_UNMAPPED_H_

#include <seqan/seq_io.h>
#include <seqan/bam_io.h>

#include "adapter_removal.h"


using namespace seqan;


// --------------------------------------------------------------------------
// Function hasLowMappingQuality()
// --------------------------------------------------------------------------

/**
 * Checks the mapping quality of a read.
 *
 * The mapping quality is considered low if the other read end does not map further
 * than 1000 bp away or in the same orientation and if, in addition, one of the
 * following holds:
 *   - The total number of matches in the cigar string is below 50.
 *   - The read end is soft-clipped by 25 or more bases at both ends.
 *   - The alignment score as indicated by the AS tag is lower than 0.5 * read length.
 *
 * @param record    a read's mapping record from a bam file
 *
 * @returns         true if the read has low mapping quality and otherwise false.
 */
inline bool
hasLowMappingQuality(BamAlignmentRecord & record, int humanSeqs)
{
    typedef Iterator<String<CigarElement<> > >::Type TIter;
    
    // Check for mapping location of other read end. If within 1000 bp and opposite orientation, accept the mapping.
    if (record.rID == record.rNextId && abs(record.beginPos - record.pNext) < 1000 &&
        hasFlagRC(record) != hasFlagNextRC(record))
        return false;

    if (record.rID > humanSeqs) return false;
    
    // Check for less than 50 bp matches ('M') in cigar string.
    unsigned matches = 0;
    TIter itEnd = end(record.cigar);
    for (TIter it = begin(record.cigar); it != itEnd; ++it)
        if ((*it).operation == 'M') matches += (*it).count;
    if (matches < 50) return true;

    // Check for soft-clipping at BOTH ENDS by more than 24 bp.
    if (record.cigar[0].operation == 'S' &&
        record.cigar[0].count > 24 &&
        record.cigar[length(record.cigar)-1].operation == 'S' &&
        record.cigar[length(record.cigar)-1].count > 24)
        return true;
    
    // Check for AS (alignment score) lower than 0.5 * readLength.
    BamTagsDict tagsDict(record.tags);
    unsigned idx;
    if (findTagKey(idx, tagsDict, "AS"))
    {
        unsigned score = 0;
        extractTagValue(score, tagsDict, idx);
        if (score < 0.5*length(record.seq))
            return true;
    }

    return false;
}

// --------------------------------------------------------------------------
// Function removeLowQuality()
// --------------------------------------------------------------------------

template<typename TSize_>
inline bool
removeLowQuality(BamAlignmentRecord & record, TSize_ qualThresh)
{
    typedef Iterator<CharString, Rooted>::Type TIter;
    typedef Size<CharString>::Type TSize;

    TSize windowSize = std::max(TSize(5), length(record.qual) / 10);
    TSize windowThresh = qualThresh*windowSize;

    // Initialize windowQual with first windowSize quality values.
    TSize windowQual = 0; 
    TIter qualEnd = end(record.qual);
    TIter windowEnd = begin(record.qual) + std::min(windowSize, length(record.qual));
    TIter windowBegin = begin(record.qual);
    for (; windowBegin != windowEnd; ++windowBegin) 
        windowQual += *windowBegin - 33;

    // Check quality from the left.
    for (windowBegin = begin(record.qual); windowEnd < qualEnd; ++windowEnd, ++windowBegin)
    {
        if (windowQual >= (TSize)windowThresh)
        {
            while (*windowBegin - 33 < qualThresh) ++windowBegin;
            record.seq = suffix(record.seq, position(windowBegin));
            record.qual = suffix(record.qual, position(windowBegin));
            break;
        }
        
        windowQual -= *windowBegin - 33;
        windowQual += *windowEnd - 33;
    }
    if (windowEnd == qualEnd) return 1;

    // Initialize windowQual with last windowSize quality values.
    windowQual = 0;
    TIter qualBegin = begin(record.qual);
    windowEnd = end(record.qual) - 1;
    windowBegin = windowEnd - std::min(windowSize, length(record.qual));
    for (; windowEnd != windowBegin; --windowEnd)
        windowQual += *windowEnd - 33;

    // Check quality from the right.
    for (windowEnd = end(record.qual) - 1; windowBegin >= qualBegin; --windowBegin, --windowEnd)
    {
        if (windowQual >= (TSize)windowThresh)
        {
            while (*windowEnd - 33 < qualThresh) --windowEnd;
            record.seq = prefix(record.seq, position(windowEnd) + 1);
            record.qual = prefix(record.qual, position(windowEnd) + 1);
            break;
        }

        windowQual -= *windowEnd - 33;
        windowQual += *windowBegin -33;
    }

    if (length(record.seq) < 30) return 1;
    return 0;
}

// --------------------------------------------------------------------------
// Functions setUnmapped() and setMateUnmapped()
// --------------------------------------------------------------------------

inline void
setUnmapped(BamAlignmentRecord & record)
{
    record.flag |= BAM_FLAG_UNMAPPED;
    record.flag &= ~BAM_FLAG_ALL_PROPER;
    record.rID = record.rNextId;
    record.beginPos = record.pNext;
    record.mapQ = 0;
    clear(record.cigar);
    record.tLen = BamAlignmentRecord::INVALID_LEN;
    //clear(record.tags);
}

// --------------------------------------------------------------------------

inline void
setMateUnmapped(BamAlignmentRecord & record)
{
    record.flag |= BAM_FLAG_NEXT_UNMAPPED;
    record.flag &= ~BAM_FLAG_ALL_PROPER;
    record.rNextId = record.rID;
    record.pNext = record.beginPos;
    record.tLen = BamAlignmentRecord::INVALID_LEN;
}

// --------------------------------------------------------------------------
// Function appendFastqRecord()
// --------------------------------------------------------------------------

// Append a read to map of fastq records.
void
appendFastqRecord(std::map<CharString, Pair<CharString> > & firstReads,
                  std::map<CharString, Pair<CharString> > & secondReads,
                  BamAlignmentRecord const & record)
{
    CharString seq = record.seq;
    CharString qual = record.qual;

    if (hasFlagRC(record))
    {
        reverseComplement(seq);
        reverse(qual);
    }

    if (hasFlagFirst(record))
    {
        if (firstReads.count(record.qName) != 0)
        {
            std::cerr << "[" << time(0) << "] ";
            std::cerr << "WARNING: Multiple records for read " << record.qName << " in bam file." << std::endl;
        }
        firstReads[record.qName] = Pair<CharString>(seq, qual);
    }
    else // hasFlagLast(record)
    {
        if (secondReads.count(record.qName) != 0)
        {
            std::cerr << "[" << time(0) << "] ";
            std::cerr << "WARNING: Multiple records for read " << record.qName << " in bam file." << std::endl;
        }
        secondReads[record.qName] = Pair<CharString>(seq, qual);
    }
}

// --------------------------------------------------------------------------
// Function writeFastq()
// --------------------------------------------------------------------------

int
writeFastq(CharString & fastqFirst,
           CharString & fastqSecond,
           CharString & fastqSingle,
           std::map<CharString, Pair<CharString> > const & firstReads,
           std::map<CharString, Pair<CharString> > const & secondReads)
{
    typedef std::map<CharString, Pair<CharString> > TFastqMap;

    // Open the output files.
    SequenceStream fastqFirstStream(toCString(fastqFirst), SequenceStream::WRITE, SequenceStream::FASTQ);
    if (!isGood(fastqFirstStream))
    {
        std::cerr << "ERROR while opening temporary output file " << fastqFirst << std::endl;
        return 1;
    }
    SequenceStream fastqSecondStream(toCString(fastqSecond), SequenceStream::WRITE, SequenceStream::FASTQ);
    if (!isGood(fastqSecondStream))
    {
        std::cerr << "ERROR while opening temporary output file " << fastqFirst << std::endl;
        return 1;
    }
    SequenceStream fastqSingleStream(toCString(fastqSingle), SequenceStream::WRITE, SequenceStream::FASTQ);
    if (!isGood(fastqSingleStream))
    {
        std::cerr << "ERROR while opening temporary output file " << fastqFirst << std::endl;
        return 1;
    }

    // Initialize iterators over reads in fastq maps.
    TFastqMap::const_iterator firstIt = firstReads.begin();
    TFastqMap::const_iterator firstEnd = firstReads.end();
    TFastqMap::const_iterator secondIt = secondReads.begin();
    TFastqMap::const_iterator secondEnd = secondReads.end();

    // Iterate over reads and output to fastq files (paired.1 and paired.2, or single).
    while (firstIt != firstEnd && secondIt != secondEnd)
    {
        if (firstIt->first < secondIt->first)
        {
            writeRecord(fastqSingleStream, firstIt->first, firstIt->second.i1, firstIt->second.i2);
            ++firstIt;
        }
        else if (firstIt->first == secondIt->first)
        {
            writeRecord(fastqFirstStream, firstIt->first, firstIt->second.i1, firstIt->second.i2);
            writeRecord(fastqSecondStream, secondIt->first, secondIt->second.i1, secondIt->second.i2);
            ++firstIt; ++secondIt;
        }
        else // firstIt->first > secondIt->first
        {
            writeRecord(fastqSingleStream, secondIt->first, secondIt->second.i1, secondIt->second.i2);
            ++secondIt;
        }
    }

    // Iterate over remaining reads and output to single.fastq.
    while (firstIt != firstEnd)
    {
        writeRecord(fastqSingleStream, firstIt->first, firstIt->second.i1, firstIt->second.i2);
        ++firstIt;
    }
    while (secondIt != secondEnd)
    {
        writeRecord(fastqSingleStream, secondIt->first, secondIt->second.i1, secondIt->second.i2);
        ++secondIt;
    }

    return 0;
}

// --------------------------------------------------------------------------
// Function findOtherReads()
// --------------------------------------------------------------------------

template<typename TPos>
int
findOtherReads(BamStream & matesStream,
               std::map<Pair<TPos>, Pair<CharString, bool> > & otherReads,
               CharString const & mappingBam)
{
    typedef std::map<Pair<TPos>, Pair<CharString, bool> > TOtherMap;

    int numFound = 0; // Return value.

    // Open input file.
    BamStream inStream(toCString(mappingBam));
    if (!isGood(inStream))
    {
        std::cerr << "ERROR while opening input bam file " << mappingBam << std::endl;
        return -1;
    }

    // Load bam index.
    CharString baiFile = mappingBam;
    baiFile += ".bai";
    BamIndex<Bai> bamIndex;
    if (read(bamIndex, toCString(baiFile)) != 0)
    {
        std::cerr << "ERROR: Could not read BAI index file " << baiFile << std::endl;
        return -1;
    }

    __int32 rID = BamAlignmentRecord::INVALID_REFID;
    BamAlignmentRecord record;

    typename TOtherMap::const_iterator itEnd = otherReads.end();
    for (typename TOtherMap::const_iterator it = otherReads.begin(); it != itEnd; ++it)
    {
        if (rID != it->first.i1)
        {
            // Jump to chromosome.
            rID = it->first.i1;
            bool hasAligns;
            jumpToRegion(inStream, hasAligns, rID, it->first.i2, maxValue<TPos>(), bamIndex);
            if (readRecord(record, inStream) != 0)
            {
                std::cerr << "ERROR while reading bam record from " << mappingBam << std::endl;
                return -1;
            }
        }

        // Skip reads not in list.
        while (!atEnd(inStream) && record.rID == it->first.i1 &&
              (record.beginPos < it->first.i2 || (record.beginPos == it->first.i2 && record.qName != it->second.i1)))
        {
            if (readRecord(record, inStream) != 0)
            {
                std::cerr << "ERROR while reading bam record from " << mappingBam << std::endl;
                return -1;
            }
        }

        // Output record if it matches qName, rID, and beginPos.
        if (!atEnd(inStream) &&
            record.qName == it->second.i1 && record.rID == it->first.i1 && record.beginPos == it->first.i2)
        {
            // Check if both ends are low-quality mapped and, hence, are already in fastq files.
            if (otherReads.count(Pair<TPos>(record.rNextId, record.pNext)) == 0)
            {
                setMateUnmapped(record);
                writeRecord(matesStream, record);
            }
            ++numFound;
        }
    }

    return numFound;
}

// ==========================================================================
// Function crop_unmapped()
// ==========================================================================

template<typename TAdapterTag>
int
crop_unmapped(Triple<CharString> & fastqFiles,
              CharString & matesBam,
              CharString const & mappingBam,
              int humanSeqs,
              TAdapterTag tag)
{
    typedef __int32 TPos;
    typedef std::map<CharString, Pair<CharString> > TFastqMap; // Reads to go into fastq files.
    typedef std::map<Pair<TPos>, Pair<CharString, bool> > TOtherMap; // Reads to crop in a second pass of the input file.
    typedef StringSet<Dna5String> TStringSet;

    // Open the input bam file.
    BamStream inStream(toCString(mappingBam));
    if (!isGood(inStream))
    {
        std::cerr << "ERROR while opening input bam file " << mappingBam << std::endl;
        return 1;
    }

    // Open the bam output file and copy the header from the input file.
    BamStream matesStream(toCString(matesBam), BamStream::WRITE);
    if (!isGood(matesStream))
    {
        std::cerr << "ERROR while opening output bam file " << matesBam << std::endl;
        return 1;
    }
    matesStream.header = inStream.header;

    // Create maps for fastq records (first read in pair and second read in pair) and bam records without mate.
    TFastqMap firstReads, secondReads;
    TOtherMap otherReads;

    // Retrieve the adapter sequences with up to one error and create indices.
    TStringSet universal = complementUniversalOneError();
    TStringSet truSeqs = reverseTruSeqsOneError(tag);
    Index<TStringSet> indexUniversal(universal);
    Index<TStringSet> indexTruSeqs(truSeqs);

    // Iterate over the input file.
    BamAlignmentRecord record;
    while (!atEnd(inStream))
    {
        // Read the next read from input file.
        if (readRecord(record, inStream) != 0)
        {
            std::cerr << "ERROR while reading bam record from " << mappingBam << std::endl;
            return 1;
        }

        // Check for flags that indicate 'uninteresting' bam records.
        if (hasFlagDuplicate(record) or hasFlagSecondary(record) or
            hasFlagQCNoPass(record) or hasFlagSupplementary(record)) continue;

        // Check the read's unmapped flag.
        if (hasFlagUnmapped(record))
        {
            if (removeLowQuality(record, 20) != 1 && removeAdapter(record, indexUniversal, indexTruSeqs, 30, tag) != 2)
                appendFastqRecord(firstReads, secondReads, record);
        }

        // Check for low mapping quality.
        else if (hasLowMappingQuality(record, humanSeqs))
        {
            if (removeLowQuality(record, 20) != 1 && removeAdapter(record, indexUniversal, indexTruSeqs, 30, tag) != 2)
            {
                appendFastqRecord(firstReads, secondReads, record);
                otherReads[Pair<TPos>(record.rNextId, record.pNext)] = Pair<CharString, bool>(record.qName, hasFlagFirst(record));
            }
        }

        // Check the mate's unmapped flag.
        else if (hasFlagNextUnmapped(record))
        {
            writeRecord(matesStream, record);
        }
    }

    std::cerr << "[" << time(0) << "] Map of low quality mates has " << otherReads.size() << " records." << std::endl;

    // Write the remaining fastq records.
    if (writeFastq(fastqFiles.i1, fastqFiles.i2, fastqFiles.i3, firstReads, secondReads) != 0) return 1;

    std::cerr << "[" << time(0) << "] Unmapped reads written to ";
    std::cerr << fastqFiles.i1 << ", " << fastqFiles.i2 << ", " << fastqFiles.i3 << std::endl;

    // Find the other read end of the low quality mapping reads and write them to the output bam file. 
    int found = findOtherReads(matesStream, otherReads, mappingBam);
    if (found == -1) return 1;

    std::cerr << "[" << time(0) << "] Mapped mates of unmapped reads written to " << matesBam << " , ";
    std::cerr << found << " found in second pass" << std::endl;

    return 0;
}

#endif // #ifndef NOVINS_CROP_UNMAPPED_H_
