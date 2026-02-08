#ifndef ZIPF_ANALYZER_H
#define ZIPF_ANALYZER_H

#include <string>
#include <vector>
#include "string_map.h"

struct TermFrequency {
    std::string term;
    size_t frequency;
    size_t rank;
    
    TermFrequency() : frequency(0), rank(0) {}
    TermFrequency(const std::string& t, size_t f) : term(t), frequency(f), rank(0) {}
};

class ZipfAnalyzer {
private:
    StringMap<size_t> term_counts_;
    size_t total_terms_;
    
public:
    ZipfAnalyzer();
    
    void add_term(const std::string& term);
    void print_stats();
    
    std::vector<TermFrequency> get_sorted_terms() const;
    
    size_t unique_terms() const;
    size_t total_terms() const;
};

#endif
