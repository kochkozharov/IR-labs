#ifndef BOOLEAN_SEARCH_H
#define BOOLEAN_SEARCH_H

#include <string>
#include <vector>
#include "inverted_index.h"
#include "tokenizer.h"
#include "stemmer.h"

struct SearchResult {
    std::string doc_id;
    double score;
    
    SearchResult() : score(0.0) {}
    SearchResult(const std::string& d, double s) : doc_id(d), score(s) {}
};

class BooleanSearch {
private:
    const InvertedIndex& index_;
    Tokenizer tokenizer_;
    PorterStemmer stemmer_;
    
    std::vector<size_t> intersect(const std::vector<size_t>& a, const std::vector<size_t>& b);
    std::vector<size_t> unite(const std::vector<size_t>& a, const std::vector<size_t>& b);
    std::vector<size_t> subtract(const std::vector<size_t>& a, const std::vector<size_t>& b);
    std::vector<size_t> all_doc_ids();

    enum class TokType { WORD, AND_OP, OR_OP, NOT_OP, LPAREN, RPAREN, END };
    struct QToken {
        TokType type;
        std::string text;
    };

    std::vector<QToken> lex(const std::string& query);

    std::vector<QToken> qtokens_;
    size_t qpos_;

    std::vector<size_t> parse_or_expr();
    std::vector<size_t> parse_and_expr();
    std::vector<size_t> parse_unary();
    std::vector<size_t> parse_primary();
    std::vector<size_t> term_docs(const std::string& stemmed);

public:
    BooleanSearch(const InvertedIndex& index);
    
    std::vector<SearchResult> search(const std::string& query, size_t max_results = 100);
};

#endif
