#ifndef POPINS_MERGE_PARTITION_H_
#define POPINS_MERGE_PARTITION_H_

#include <seqan/index.h>
#include <seqan/align.h>

#include "contig_id.h"
#include "contig_structs.h"

using namespace seqan;

// --------------------------------------------------------------------------
// Function calculateEntropy()
// --------------------------------------------------------------------------

template<typename TSeq>
double
averageEntropy(TSeq & seq)
{
    typedef typename Size<TSeq>::Type TSize;

    // Count dinucleotide occurrences
    String<TSize> diCounts;
    resize(diCounts, 16, 0);
    int counted = 0;
    for (TSize i = 0; i < length(seq)-1; ++i)
    {
        if (seq[i] != 'N' && seq[i+1] != 'N')
        {
            diCounts[ordValue(seq[i]) + 4*ordValue(seq[i+1])] += 1;
            counted += 1;
        }
    }

    // Calculate entropy for dinucleotide counts
    double entropy = 0;
    typename Iterator<String<TSize> >::Type countEnd = end(diCounts);
    for (typename Iterator<String<TSize> >::Type count = begin(diCounts); count != countEnd; ++count)
    {
        if (*count == 0) continue;
        double p = double(*count) / counted;
        entropy -= p * log(p) / log(2);
    }

    return entropy / 4;
}

// ==========================================================================
// Function filterByEntropy()
// ==========================================================================

template<typename TSize, typename TSeq>
bool
filterByEntropy(std::map<TSize, Contig<TSeq> > & contigs,
        MergingOptions & options)
{
    typedef typename std::map<TSize, Contig<TSeq> >::iterator TIter;
    String<TSize> lowEntropyContigs;

    // Iterate contigs and determine entropy
    TIter itEnd = contigs.end();
    for (TIter it = contigs.begin(); it != itEnd; ++it)
    {
        // Entropy calculation
        double entropy = averageEntropy((it->second).seq);

        if (entropy < options.minEntropy)
        {
            options.skippedStream << ">" << (it->second).id << " (entropy filter, entropy: " << entropy << ")" << std::endl;
            options.skippedStream << (it->second).seq << std::endl;
            appendValue(lowEntropyContigs, it->first);
        }
    }

    typename Iterator<String<TSize> >::Type lowEnd = end(lowEntropyContigs);
    for (typename Iterator<String<TSize> >::Type it = begin(lowEntropyContigs); it != lowEnd; ++it)
        contigs.erase(*it);

    if (length(contigs) == 0)
    {
        std::cerr << "There are no contigs that passed the entropy filter." << std::endl;
        return 1;
    }

    std::ostringstream msg;
    msg << "Passed entropy filter: " << length(contigs);
    printStatus(msg);

    return 0;
}

// --------------------------------------------------------------------------
// Function readNextContig()
// --------------------------------------------------------------------------

template<typename TSeq, typename TSize>
int
readNextContig(Contig<TSeq> & contig, SeqFileIn & stream, TSize & i, String<CharString> & filenames)
{
    // Open the next file.
    while ((i < (int)length(filenames) && atEnd(stream)))
    {
        ++i;
        close(stream);
        open(stream, toCString(filenames[i]));
    }

    if (atEnd(stream)) return 1;

    // Read the next record.
    contig.id.orientation = true;
    contig.id.pn = formattedIndex(i, length(filenames));
    readRecord(contig.id.contigId, contig.seq, stream);

    return 0;
}

// --------------------------------------------------------------------------
// Function pairwiseAlignment()
// --------------------------------------------------------------------------

template<typename TSeq, typename TValueScore>
inline bool
pairwiseAlignment(TSeq & contig1,
        TSeq & contig2,
        Score<int, Simple> scoringScheme,
        int lowerDiag,
        int upperDiag,
        TValueScore minScore)
{
    // setup alignment object
    Align<TSeq, ArrayGaps> align;
    resize(rows(align), 2);
    setSource(row(align, 0), contig1);
    setSource(row(align, 1), contig2);

    // compute local alignment
    int score = localAlignment(align, scoringScheme, lowerDiag, upperDiag);

    // return true if minimal score is reached
    if (score > minScore)
        return true;
    else
        return false;
}

// ==========================================================================
// Function partitionContigs()
// ==========================================================================

template<typename TSize, typename TSeq>
bool
partitionContigs(UnionFind<int> & uf,
        std::set<Pair<TSize> > & alignedPairs,
        std::map<TSize, Contig<TSeq> > & contigs,
        ContigBatch & batch,
        MergingOptions & options)
{
    typedef typename std::map<TSize, Contig<TSeq> >::iterator TContigIter;
    typedef StringSet<TSeq, Dependent<> > TStringSet;
    typedef Index<TStringSet, IndexQGram<SimpleShape, OpenAddressing> > TIndex;
    typedef Finder<TSeq, Swift<SwiftLocal> > TFinder;

    printStatus("Partitioning contigs");
    printStatus("- Indexing batch of contigs");

    TSize numComparisons = 0;

    // initialization of SWIFT pattern (q-gram index)
    TStringSet seqs;
    StringSet<TSize> indices;
    TContigIter itEnd = contigs.end();
    for (TContigIter it = contigs.begin(); it != itEnd; ++it)
    {
        appendValue(seqs, (it->second).seq);
        appendValue(indices, it->first);
    }
    TIndex qgramIndex(seqs);
    resize(indexShape(qgramIndex), options.qgramLength);
    Pattern<TIndex, Swift<SwiftLocal> > swiftPattern(qgramIndex);
    indexRequire(qgramIndex, QGramSADir());

    // define scoring scheme
    Score<int, Simple> scoringScheme(options.matchScore, options.errorPenalty, options.errorPenalty);
    int diagExtension = options.minScore/10;

    // print status bar
    printStatus("- Streaming over all contig files");
    std::cerr << "0%   10   20   30   40   50   60   70   80   90   100%" << std::endl;
    std::cerr << "|----|----|----|----|----|----|----|----|----|----|" << std::endl;

    unsigned fiftieth = std::max((indexOffset(batch)+batchSize(batch))/50, 1);

    // stream over the contigs
    int i = 0;
    SeqFileIn contigStream(toCString(batch.contigFiles[i]));
    //for (unsigned a = 0; a < indexOffset(batch)+length(contigs)/2; ++a)
    for (int a = 0; a < indexOffset(batch)+batchSize(batch); ++a)
    {
        if (a%fiftieth == 0)
            std::cerr << "*" << std::flush;

        // read the next contig
        Contig<TSeq> contig;
        int ret = readNextContig(contig, contigStream, i, batch.contigFiles);
        SEQAN_ASSERT_NEQ(ret, 1);
        if (ret == -1) return 1;
        if (contigs.count(a) == 0) continue; // skipped contig

        // initialization of swift finder
        TFinder swiftFinder(contig.seq, 1000, 1);

        hash(swiftPattern.data_host.data_value->shape, hostIterator(hostIterator(swiftFinder)));
        while (find(swiftFinder, swiftPattern, options.errorRate, options.minimalLength))
        {

            // get index of pattern sequence
            unsigned bSubset = swiftPattern.curSeqNo;
            unsigned b = indices[bSubset];

            // align contigs only of different individuals
            if (contig.id.pn == contigs[b].id.pn) continue;

            // align contigs only if not same component already
            if (findSet(uf, a) == findSet(uf, b)) continue;

            // find the contig sequences
            TSeq contigA = haystack(swiftFinder);
            TSeq contigB = indexText(needle(swiftPattern))[bSubset];

            // compute upper and lower diagonal of band.
            int upperDiag = (*swiftFinder.curHit).hstkPos - (*swiftFinder.curHit).ndlPos;
            int lowerDiag = upperDiag - swiftPattern.bucketParams[bSubset].delta - swiftPattern.bucketParams[bSubset].overlap;
            upperDiag += diagExtension;
            lowerDiag -= diagExtension;

            // verify by banded Smith-Waterman alignment
            ++numComparisons;
            if (!pairwiseAlignment(contigA, contigB, scoringScheme, lowerDiag, upperDiag, options.minScore)) continue;
            alignedPairs.insert(Pair<TSize>(a, b));

            // join sets of the two aligned contigs
            joinSets(uf, findSet(uf, a), findSet(uf, b));

            // join sets for reverse complements of the contigs
            unsigned a1 = globalIndexRC(a, batch);
            unsigned b1 = globalIndexRC(b, batch);
            joinSets(uf, findSet(uf, a1), findSet(uf, b1));

            // stop aligning this contig if it is already in a component with more than 100 other contigs
            if (uf._values[findSet(uf, a)] < -100) break;
        }
    }
    std::cerr << std::endl;

    std::ostringstream msg;
    msg << "Number of pairwise comparisons: " << numComparisons;
    printStatus(msg);

    msg.str("");
    msg << "Number of valid alignments:     " << length(alignedPairs);
    printStatus(msg);

    return 0;
}

// --------------------------------------------------------------------------
// Function writeAlignedPairs()
// --------------------------------------------------------------------------

template<typename TStream, typename TSize>
void
writeAlignedPairs(TStream & outputStream, std::set<Pair<TSize> > & alignedPairs)
{
    typedef typename std::set<Pair<TSize> >::iterator TIter;

    TIter pairsEnd = alignedPairs.end();
    for (TIter pairsIt = alignedPairs.begin(); pairsIt != pairsEnd; ++pairsIt)
        outputStream << (*pairsIt).i1 << " " << (*pairsIt).i2 << "\n";
}

// --------------------------------------------------------------------------
// Function readAlignedPairs()
// --------------------------------------------------------------------------

template<typename TSize>
bool
readAlignedPairs(UnionFind<int> & uf, std::set<Pair<TSize> > & alignedPairs, CharString & fileName, unsigned len)
{
    // Open the input file and initialize record reader.
    std::fstream stream(toCString(fileName), std::ios::in);

    if (!stream.is_open())
    {
        std::cerr << "ERROR: Could not open components input file " << fileName << std::endl;
        return 1;
    }

    TSize key, val, key_rev, val_rev;

    TSize numPairs = 0;

    // Read the components line by line.
    while (stream >> key >> val)
    {
        if (key < len) key_rev = key + len;
        else key_rev = key - len;

        if (val < len) val_rev = val + len;
        else val_rev = val - len;

        if (findSet(uf, key) == findSet(uf, val)) continue;

        // Add the aligned pairs.
        alignedPairs.insert(Pair<TSize>(key, val));
        ++numPairs;

        // Join sets of key and value.
        joinSets(uf, findSet(uf, key), findSet(uf, val));
        SEQAN_ASSERT_EQ(findSet(uf, key), findSet(uf, val));
        joinSets(uf, findSet(uf, key_rev), findSet(uf, val_rev));
        SEQAN_ASSERT_EQ(findSet(uf, key_rev), findSet(uf, val_rev));

    }

    std::ostringstream msg;
    msg << "Loaded " << fileName << ": " << numPairs << " pairs.";
    printStatus(msg);

    return 0;
}

// --------------------------------------------------------------------------
// Function unionFindToComponents()
// --------------------------------------------------------------------------

template<typename TSize, typename TSeq>
std::set<int>
unionFindToComponents(std::map<TSize, ContigComponent<TSeq> > & components,
        UnionFind<int> & uf,
        std::set<Pair<TSize> > & alignedPairs,
        ContigBatch & batch)
{
    std::set<int> skipped;
    std::ostringstream msg;

    // Determine components from Union-Find data structure by
    // mapping ids to their representative id.
    for (typename std::set<Pair<TSize> >::iterator it = alignedPairs.begin(); it != alignedPairs.end(); ++it)
    {
        int rev1 = globalIndexRC((*it).i1, batch);
        int rev2 = globalIndexRC((*it).i2, batch);

        int set = std::min(findSet(uf, (*it).i1), findSet(uf, rev1));

        /*
        // skip components that are 10 times larger than number of samples
        if (uf._values[set] < -10 * (int)length(batch.contigFiles)) 
        {
            if (skipped.count(set) == 0)
            {
                msg.str("");
                msg << "WARNING: Skipping component of size " << (-1*uf._values[set]);
                printStatus(msg);

                skipped.insert(set);
            }
            skipped.insert((*it).i1);
            skipped.insert((*it).i2);
            skipped.insert(rev1);
            skipped.insert(rev2);
        }
        else
         */
        {
            components[set].alignedPairs.insert(*it);
            components[set].alignedPairs.insert(Pair<TSize>((*it).i2, (*it).i1));
            components[set].alignedPairs.insert(Pair<TSize>(rev1, rev2));
            components[set].alignedPairs.insert(Pair<TSize>(rev2, rev1));
        }
    }

    msg.str("");
    msg << "There are " << components.size() << " components.";
    printStatus(msg);

    return skipped;
}

// --------------------------------------------------------------------------
// Function addSingletons()
// --------------------------------------------------------------------------

template<typename TSize, typename TSeq>
void
addSingletons(std::map<TSize, ContigComponent<TSeq> > & components,
        std::set<int> & skipped,
        UnionFind<int> & uf,
        int totalContigs)
{
    unsigned numSingletons = 0;
    for (int i = 0; i < totalContigs; ++i)
    {
        if (skipped.count(i) == 0 && components.count(i) == 0 && i == findSet(uf, i))
        {
            components[i];
            ++numSingletons;
        }
    }

    std::ostringstream msg;
    msg << "Added " << numSingletons << " singletons to components.";
    printStatus(msg);
}

template<typename TSize, typename TSeq>
void
addSingletons(std::map<TSize, ContigComponent<TSeq> > & components,
        std::map<TSize, Contig<TSeq> > & contigs,
        std::set<int> & skipped,
        UnionFind<int> & uf,
        int totalContigs)
{
    unsigned numSingletons = 0;
    for (int i = 0; i < totalContigs; ++i)
    {
        if (contigs.count(i) > 0 && skipped.count(i) == 0 && components.count(i) == 0 && i == findSet(uf, i))
        {
            components[i];
            ++numSingletons;
        }
    }

    std::ostringstream msg;
    msg << "Added " << numSingletons << " singletons to components.";
    printStatus(msg);
}

// ==========================================================================
// Function readAndMergeComponents()
// ==========================================================================

template<typename TSize, typename TSequence>
bool
readAndMergeComponents(std::map<TSize, ContigComponent<TSequence> > & components,
        std::set<int> & skipped,
        String<CharString> & componentFiles,
        ContigBatch & batch)
{
    typedef std::map<TSize, ContigComponent<TSequence> > TComponents;
    typedef typename TComponents::iterator TCompIterator;

    printStatus("Reading and merging components files");

    // Initialize Union-Find data structure.
    UnionFind<int> uf;
    resize(uf, batch.contigsInTotal * 2);
    std::set<Pair<TSize> > alignedPairs;

    // Read the aligned pairs from input files and join sets.
    for (unsigned i = 0; i < length(componentFiles); ++i)
        if (readAlignedPairs(uf, alignedPairs, componentFiles[i], batch.contigsInTotal) != 0) return 1;

    // Convert union-find data structure to components.
    skipped = unionFindToComponents(components, uf, alignedPairs, batch);

    // Add singleton contigs to components (= those contigs that don't align to any other contig).
    addSingletons(components, skipped, uf, batch.contigsInTotal);

    // Keep only batch of components (erase all except every totalBatches'th component).
    unsigned total = totalBatches(batch);
    if (total != 1)
    {
        TCompIterator it = --components.end();
        TSize i = components.size();
        while (i > 0)
        {
            TCompIterator element = it;
            --it; --i;
            if (i % total != batch.number)
                components.erase(element);
        }
    }

    return 0;
}

#endif // #ifndef POPINS_MERGE_PARTITION_H_