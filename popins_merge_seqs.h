#ifndef POPINS_MERGE_SEQS_H_
#define POPINS_MERGE_SEQS_H_

#include <seqan/align.h>

#include "contig_id.h"
#include "contig_component.h"

using namespace seqan;

// --------------------------------------------------------------------------
// struct Path
// --------------------------------------------------------------------------

template<typename TSeq, typename TVertexDescriptor>
struct Path
{
    typedef typename Position<TSeq>::Type TPos;

    TSeq seq;
    std::map<TPos, TVertexDescriptor> positionMap;
    
    Path()
    {}
    
    Path(Path<TSeq, TVertexDescriptor> & other) :
        seq(other.seq), positionMap(other.positionMap)
    {}
    
    Path(Path<TSeq, TVertexDescriptor> const & other) :
        seq(other.seq), positionMap(other.positionMap)
    {}
};

// --------------------------------------------------------------------------
// struct ComponentGraph
// --------------------------------------------------------------------------

template<typename TSeq>
struct ComponentGraph
{
    typedef Graph<Directed<> > TGraph_;
    typedef typename VertexDescriptor<TGraph_>::Type TVertexDescriptor;

    TGraph_ graph;
    String<TVertexDescriptor> sources;
    String<TSeq> sequenceMap;

    ComponentGraph()
    {}

    ComponentGraph(TSeq & seq)
    {
        TVertexDescriptor v = addVertex(*this, seq);
        appendValue(sources, v);
    }
};

// --------------------------------------------------------------------------

template<typename TSeq1, typename TSeq2>
typename ComponentGraph<TSeq1>::TVertexDescriptor
addVertex(ComponentGraph<TSeq1> & graph, TSeq2 & seq)
{
    typename ComponentGraph<TSeq1>::TVertexDescriptor v = addVertex(graph.graph);
    SEQAN_ASSERT_EQ(v, length(graph.sequenceMap));
    appendValue(graph.sequenceMap, seq);
    return v;
}

// --------------------------------------------------------------------------

template<typename TSeq, typename TSeq1, typename TSeq2>
typename ComponentGraph<TSeq>::TVertexDescriptor
splitVertex(ComponentGraph<TSeq> & graph,
            typename ComponentGraph<TSeq>::TVertexDescriptor & u,
            TSeq1 & uSeq,
            TSeq2 & vSeq)
{
    typedef typename Iterator<typename ComponentGraph<TSeq>::TGraph_, OutEdgeIterator>::Type TOutEdgeIter;
    typedef typename ComponentGraph<TSeq>::TVertexDescriptor TVertexDescriptor;

    TVertexDescriptor v = addVertex(graph, vSeq);
    for (TOutEdgeIter it(graph.graph, u); !atEnd(it); ++it)
    {
        addEdge(graph.graph, v, targetVertex(it));
    }

    removeOutEdges(graph.graph, u);
    graph.sequenceMap[u] = uSeq;

    addEdge(graph.graph, u, v);

    return v;
}

// --------------------------------------------------------------------------

template<typename TSeq>
typename Size<String<TSeq> >::Type
enumeratePathsDfs(String<Path<TSeq, typename ComponentGraph<TSeq>::TVertexDescriptor> > & paths,
                  Path<TSeq, typename ComponentGraph<TSeq>::TVertexDescriptor> & prevPath,
                  ComponentGraph<TSeq> & graph,
                  typename ComponentGraph<TSeq>::TVertexDescriptor & v)
{
    typedef typename ComponentGraph<TSeq>::TVertexDescriptor TVertexDescriptor;
    typedef typename Size<String<Path<TSeq, TVertexDescriptor> > >::Type TSize;
    typedef typename Iterator<typename ComponentGraph<TSeq>::TGraph_, OutEdgeIterator>::Type TOutEdgeIter;
    
    TSize len = length(paths);
    
    append(prevPath.seq, graph.sequenceMap[v]);
    prevPath.positionMap[length(prevPath.seq)] = v;
    
    if (outDegree(graph.graph, v) == 0)
    {
        appendValue(paths, prevPath);
        return 1;
    }
    
    for (TOutEdgeIter it(graph.graph, v); !atEnd(it); ++it)
    {
        Path<TSeq, TVertexDescriptor> path(prevPath);
        TVertexDescriptor u = targetVertex(it);
        enumeratePathsDfs(paths, path, graph, u);
    }    
    
    return length(paths) - len;
}

// --------------------------------------------------------------------------
// Function enumeratePaths()
// --------------------------------------------------------------------------

template<typename TSeq>
typename Size<String<TSeq> >::Type
enumeratePaths(String<Path<TSeq, typename ComponentGraph<TSeq>::TVertexDescriptor> > & paths,
               ComponentGraph<TSeq> & graph)
{
    typedef typename ComponentGraph<TSeq>::TVertexDescriptor TVertexDescriptor;
    typedef typename Size<String<TSeq> >::Type TSize;
    
    for (TSize i = 0; i < length(graph.sources); ++i)
    {
        Path<TSeq, TVertexDescriptor> path;
        enumeratePathsDfs(paths, path, graph, graph.sources[i]);
    }

    return length(paths);
}

// --------------------------------------------------------------------------
// Function getSeqsByAlignOrder()
// --------------------------------------------------------------------------

template<typename TSeq, typename TContigs, typename TContigIds>
void
getSeqsByAlignOrder(ContigComponent<TSeq> & component, TContigs & contigs, TContigIds & contigIds)
{
    typedef typename Size<TSeq>::Type TSize;
    typedef typename std::set<Pair<TSize> >::iterator TPairIter;

    // --- find a possible order ---
    
    String<TSize> order;
    std::set<TSize> ordered;
    appendValue(order, (*component.alignedPairs.begin()).i1);
    ordered.insert((*component.alignedPairs.begin()).i1);
    
    TSize i = 0;
    while (i < length(order))
    {
        TPairIter neighborsEnd = component.alignedPairs.upper_bound(Pair<TSize>(order[i], maxValue<TSize>()));
        TPairIter neighbor = component.alignedPairs.lower_bound(Pair<TSize>(order[i], 0));
        for (; neighbor != neighborsEnd; ++neighbor)
        {
            if (ordered.count((*neighbor).i2) == 0)
            {
                // add the neighbor
                appendValue(order, (*neighbor).i2);
                ordered.insert((*neighbor).i2);
            }
        }
        ++i;
    }
    clear(ordered);
    
    // --- bring contigs and contig ids into the order ---

    for (TSize i = 0; i < length(order); ++i)
    {
        appendValue(component.ids, contigIds[order[i]]);
        appendValue(component.contigs, contigs[order[i]]);
    }
}

// --------------------------------------------------------------------------
// Function bestDiagonal()
// --------------------------------------------------------------------------

template<typename TSeq1, typename TSeq2>
int
bestDiagonal(TSeq1 & seq1, TSeq2 & seq2, unsigned qgramLength)
{
    typedef Index<TSeq1, IndexQGram<SimpleShape, OpenAddressing> > TIndex;
    typedef typename Infix<typename Fibre<TIndex, FibreSA>::Type const>::Type TOccurrences;
    typedef typename Iterator<TOccurrences>::Type TOccIter;

    unsigned len1 = length(seq1);
    unsigned len2 = length(seq2);
    
    if (qgramLength > len1 || qgramLength > len2) return maxValue<int>();

    // Build a k-mer index of seq1
    TIndex qgramIndex(seq1);
    resize(indexShape(qgramIndex), qgramLength);
    indexRequire(qgramIndex, QGramSADir());

    // Init diagonal counters
    String<unsigned> counters;
    resize(counters, len1+len2, 0);

    // Init hash function
    Shape<typename Value<TSeq1>::Type, SimpleShape> myShape(qgramLength);
    hashInit(myShape, begin(seq2));

    // Iterate over seq2 to count k-mer hits per diagonal
    for (unsigned i = 0; i < length(seq2) - length(myShape) + 1; ++i)
    {
        // Compute hash of the k-mer
        hashNext(myShape, begin(seq2) + i);

        // Increase counters of diagonals with hits
        TOccurrences occs = getOccurrences(qgramIndex, myShape);
        TOccIter itEnd = end(occs);
        for (TOccIter it = begin(occs); it != itEnd; ++it)
            ++counters[len1 + i - *it];
    }

    // Return the diagonal with the most k-mer hits
    int diag = maxValue<int>();
    unsigned maxCount = 0;
    for (unsigned i = 0; i < length(counters); ++i)
    {
        if (maxCount < counters[i])
        {
             maxCount = counters[i];
             diag = i - len1;
        }
    }
    
    if (diag == maxValue<int>()) return bestDiagonal(seq1, seq2, qgramLength*2/3);
    
    return diag;
}

// --------------------------------------------------------------------------
// Function mergeSeqWithGraph()
// --------------------------------------------------------------------------

template<typename TSeq1, typename TSeq2, typename TVertexDescriptor, typename TLength>
bool mergeSeqWithGraph(ComponentGraph<TSeq1> & compGraph,
                       Path<TSeq1, TVertexDescriptor> & path,
                       TSeq2 & seq,
                       Gaps<TSeq1> & gapsPath,
                       Gaps<TSeq2> & gapsSeq,
                       TLength minBranchLen)
{
    typedef typename Position<TSeq1>::Type TPos;
    typedef typename Size<TSeq1>::Type TSize;

    // --- handle right end of alignment

    TPos alignEndSeq = toSourcePosition(gapsSeq, length(gapsSeq)); // end position of alignment in seq
    TPos alignEndPath = toSourcePosition(gapsPath, length(gapsPath)); // end position of alignment in path.seq

    if (alignEndSeq < length(seq))
    {
        TPos vPos = path.positionMap.lower_bound(alignEndPath)->first; // end position of vertex label on path
        TVertexDescriptor v = path.positionMap[vPos];

        if (alignEndPath == length(path.seq)) // alignment ends at end of path
        {
            append(compGraph.sequenceMap[v], suffix(seq, alignEndSeq));
        }
        else if (length(seq) - alignEndSeq > (TSize)minBranchLen) // unaligned part of seq is longer than minBranchLen
        {
            if (vPos > alignEndPath) // alignment ends before end of vertex label
            {
                TPos splitPos = length(compGraph.sequenceMap[v]) - (vPos - alignEndPath); // relative to vertex label
                TSeq1 prefixSeq = prefix(compGraph.sequenceMap[v], splitPos);
                TSeq1 suffixSeq = suffix(compGraph.sequenceMap[v], splitPos);
                splitVertex(compGraph, v, prefixSeq, suffixSeq);
            }
            TSeq1 suf = suffix(seq, alignEndSeq);
            TVertexDescriptor vBranch = addVertex(compGraph, suf);
            addEdge(compGraph.graph, v, vBranch);
        }
    }

    // --- handle left end of alignment

    TPos alignBeginSeq = toSourcePosition(gapsSeq, 0); // begin position of alignment in seq
    TPos alignBeginPath = toSourcePosition(gapsPath, 0); // begin position of alignment in path.seq

    if (alignBeginSeq > 0)
    {
        TPos uPos = path.positionMap.upper_bound(alignBeginPath)->first;
        TVertexDescriptor u = path.positionMap[uPos];
        
        if (alignBeginPath == 0)
        {
            replace(compGraph.sequenceMap[u], 0, 0, prefix(seq, alignBeginSeq));
        }
        else if (alignBeginSeq > (TSize)minBranchLen)
        {
            TVertexDescriptor uSplit = u;
            if (uPos - length(compGraph.sequenceMap[u]) < alignBeginPath)
            {
                TPos splitPos = length(compGraph.sequenceMap[u]) - (uPos - alignBeginPath);
                TSeq1 prefixSeq = prefix(compGraph.sequenceMap[u], splitPos);
                TSeq1 suffixSeq = suffix(compGraph.sequenceMap[u], splitPos);
                uSplit = splitVertex(compGraph, u, prefixSeq, suffixSeq);
            }
            TSeq1 pref = prefix(seq, alignBeginSeq);
            TVertexDescriptor uBranch = addVertex(compGraph, pref);
            appendValue(compGraph.sources, uBranch);
            addEdge(compGraph.graph, uBranch, uSplit);
        }
    }

    return true;
}

// --------------------------------------------------------------------------
// Function addSequencesToGraph()
// --------------------------------------------------------------------------

template<typename TSeq1, typename TSeq2, typename TSpec, typename TLength, typename TValueMatch, typename TValueError>
bool
addSequencesToGraph(ComponentGraph<TSeq1> & compGraph,
                    StringSet<TSeq2, TSpec> & seqs,
                    TLength minBranchLen,
                    TValueMatch matchScore,
                    TValueError errorPenalty,
                    unsigned qgramLength)
{
    typedef int TScoreValue;
    typedef ComponentGraph<TSeq1> TGraph;
    typedef Path<TSeq1, typename TGraph::TVertexDescriptor> TPath;
    typedef typename Size<String<TPath> >::Type TSize;

    Score<TScoreValue, Simple> scoringScheme(matchScore, errorPenalty, errorPenalty);

    for (TSize i = 1; i < length(seqs); ++i)
    {
        String<TPath> paths;
        enumeratePaths(paths, compGraph);
        
        if (length(paths) > 30) return false;

        TScoreValue maxScore = minValue<TScoreValue>();
        TPath bestPath;
        Gaps<TSeq1> bestGapsPath;
        Gaps<TSeq2> bestGapsSeq;

        for (TSize j = 0; j < length(paths); ++j)
        {
            Gaps<TSeq1> gapsPath(paths[j].seq);
            Gaps<TSeq2> gapsSeq(seqs[i]);

            int diag = bestDiagonal(seqs[i], paths[j].seq, qgramLength);

            TScoreValue val = 0;
            if (diag == maxValue<int>()) val = localAlignment(gapsPath, gapsSeq, scoringScheme);
            else val = localAlignment(gapsPath, gapsSeq, scoringScheme, diag-25, diag+25);

            if (val > maxScore)
            {
                maxScore = val;
                bestPath = paths[j];
                bestGapsPath = gapsPath;
                bestGapsSeq = gapsSeq;
            }
        }

        mergeSeqWithGraph(compGraph, bestPath, seqs[i], bestGapsPath, bestGapsSeq, minBranchLen);
    }

    return true;
}

// --------------------------------------------------------------------------
// Function mergeSequences()
// --------------------------------------------------------------------------

template<typename TSeq1, typename TSeq2, typename TSpec, typename TLength, typename TValueMatch, typename TValueError>
bool
mergeSequences(String<TSeq1> & mergedSeqs,
               StringSet<TSeq2, TSpec> & seqs,
               TLength & minBranchLen,
               TValueMatch matchScore,
               TValueError errorPenalty,
               unsigned qgramLength,
               bool verbose)
{
    typedef int TScoreValue;    
    typedef ComponentGraph<TSeq1> TGraph;
    typedef Path<TSeq1, typename TGraph::TVertexDescriptor> TPath;
    typedef typename Size<String<TPath> >::Type TSize;

    TGraph compGraph(seqs[0]);
    if (!addSequencesToGraph(compGraph, seqs, minBranchLen, matchScore, errorPenalty, qgramLength))
        return false;

    String<TPath> finalPaths;
    enumeratePaths(finalPaths, compGraph);

    if (verbose && numVertices(compGraph.graph) > 1)
    {
        std::cout << compGraph.graph;
        std::cout << "Vertex map:" << std::endl;
        for (TSize i = 0; i < length(compGraph.sequenceMap); ++i)
        {
            std::cout << "Vertex: " << i << ", Length: " << length(compGraph.sequenceMap[i]) << std::endl;
        }
    }

    for (TSize i = 0; i < length(finalPaths); ++i)
        appendValue(mergedSeqs, finalPaths[i].seq);

    return true;
}

// --------------------------------------------------------------------------
// Function writeSupercontigs()
// --------------------------------------------------------------------------

template<typename TStream, typename TSeq, typename TSpec>
void
writeSupercontigs(TStream & outputStream, String<TSeq> & mergedSeqs, StringSet<TSeq, TSpec> & contigs, int batchIndex, unsigned pos)
{
    typedef typename Size<TSeq>::Type TSize;

    if (length(mergedSeqs) <= 25)
    {
        for (TSize i = 0; i < length(mergedSeqs); ++i)
        {
            outputStream << ">COMPONENT_" << batchIndex << "." << pos << "_" << char('a'+i)
                         << "_length_" << length(mergedSeqs[i])
                         << "_size_" << length(contigs) << std::endl;
            outputStream << mergedSeqs[i] << std::endl;
        }
    }
    else
    {
        for (TSize i = 0; i < length(mergedSeqs); ++i)
        {
            outputStream << ">COMPONENT_" << batchIndex << "." << pos << "_" << char('a'+i/26) << char('a'+i%26)
                         << "_length_" << length(mergedSeqs[i])
                         << "_size_" << length(contigs) << std::endl;
            outputStream << mergedSeqs[i] << std::endl;
        }
    }
}

// ==========================================================================
// Function constructSupercontigs()
// ==========================================================================

template<typename TSize, typename TSequence, typename TContigs, typename TContigIds>
void
constructSupercontigs(std::map<TSize, ContigComponent<TSequence> > & components,
                      TContigs & contigs,
                      TContigIds & contigIds,
                      MergingOptions & options)
{
    typedef std::map<TSize, ContigComponent<TSequence> > TComponents;
    
    if (options.verbose) std::cerr << "[" << time(0) << "] " << "Constructing supercontigs" << std::endl;

    unsigned numSingleton = 0;
    unsigned numBranching = 0;
    unsigned numVeryBranching = 0;
    unsigned numTooLarge = 0;
    
    // Iterate over the set of components.
    unsigned pos = 0;
    for (typename TComponents::iterator it = components.begin(); it != components.end(); ++it)
    {
        ContigComponent<TSequence> component = it->second;

        // Sort the contigs for merging.
        getSeqsByAlignOrder(component, contigs, contigIds);
        
        if (length(component.contigs) > 10 * length(options.contigFiles))
        {
            if (options.verbose) std::cout << "COMPONENT_" << pos << " size:" << length(component.contigs) << " skipped." << std::endl;
            ++numTooLarge;
            continue;
        }

        // Output component if consisting of a single contig.
        if (length(component.contigs) == 1)
        {
            SEQAN_ASSERT_EQ(length(component.ids), 1u);
            options.outputStream << ">" << component.ids[0] << std::endl;
            options.outputStream << component.contigs[0] << std::endl;

            ++numSingleton;
            continue;
        }

        if (options.verbose) std::cout << "COMPONENT_" << options.batchIndex << "." << pos << " size:" << length(component.contigs) << std::endl;

        // --- MERGE CONTIGS OF THE COMPONENT ---
        String<TSequence> mergedSeqs;
        if (!mergeSequences(mergedSeqs, component.contigs,
                            options.minTipScore, options.matchScore, options.errorPenalty, options.qgramLength,
                            options.verbose))
        {
            if (options.verbose) std::cout << "COMPONENT_" << options.batchIndex << "." << pos << " size:" << length(component.contigs) << " given up." << std::endl;
            ++numVeryBranching;
            ++numBranching;
            clear(component);
            ++pos;
            continue;
        }

        if (length(mergedSeqs) > 1) ++numBranching;

        // Output the supercontig.
        writeSupercontigs(options.outputStream, mergedSeqs, component.contigs, options.batchIndex, pos);
        
        clear(component);
        ++pos;
    }

    options.outputStream.close();

    if (options.verbose) 
    {
        std::cerr << "[" << time(0) << "] " << length(components)-numSingleton << " components are merged from several contigs." << std::endl;
        std::cerr << "[" << time(0) << "] " << numSingleton << " contigs did not align with any other contig." << std::endl;
        std::cerr << "[" << time(0) << "] " << numBranching << " components are branching, given up on " << numVeryBranching << " of them." << std::endl;
        std::cerr << "[" << time(0) << "] " << numTooLarge << " components exceeded the maximum number of contigs for merging." << std::endl;
    }
}

#endif // #ifndef POPINS_MERGE_SEQS_H_
