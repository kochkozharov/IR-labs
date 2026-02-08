#ifndef BOOLEAN_SEARCH_H
#define BOOLEAN_SEARCH_H

#include <string>
#include <vector>
#include "inverted_index.h"
#include "tokenizer.h"
#include "stemmer.h"

enum class QueryOperator {
    AND,
    OR,
    NOT
};

struct QueryTerm {
    std::string term;
    QueryOperator op;
    
    QueryTerm() : op(QueryOperator::AND) {}
    QueryTerm(const std::string& t, QueryOperator o) : term(t), op(o) {}
};

struct SearchResult {
    std::string doc_id;
    size_t score;
    
    SearchResult() : score(0) {}
    SearchResult(const std::string& d, size_t s) : doc_id(d), score(s) {}
};

class BooleanSearch {
private:
    const InvertedIndex& index_;
    Tokenizer tokenizer_;
    PorterStemmer stemmer_;
    
    std::vector<size_t> intersect(const std::vector<size_t>& a, const std::vector<size_t>& b);
    std::vector<size_t> unite(const std::vector<size_t>& a, const std::vector<size_t>& b);
    std::vector<size_t> subtract(const std::vector<size_t>& a, const std::vector<size_t>& b);
    
    std::vector<QueryTerm> parse_query(const std::string& query);
    
public:
    BooleanSearch(const InvertedIndex& index);
    
    std::vector<SearchResult> search(const std::string& query, size_t max_results = 100);
};

#endif
