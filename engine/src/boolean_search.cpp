#include "boolean_search.h"

BooleanSearch::BooleanSearch(const InvertedIndex& index) : index_(index) {}

std::vector<size_t> BooleanSearch::intersect(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            result.push_back(a[i]);
            ++i;
            ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    
    return result;
}

std::vector<size_t> BooleanSearch::unite(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            result.push_back(a[i]);
            ++i;
            ++j;
        } else if (a[i] < b[j]) {
            result.push_back(a[i]);
            ++i;
        } else {
            result.push_back(b[j]);
            ++j;
        }
    }
    
    while (i < a.size()) {
        result.push_back(a[i++]);
    }
    while (j < b.size()) {
        result.push_back(b[j++]);
    }
    
    return result;
}

std::vector<size_t> BooleanSearch::subtract(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    
    while (i < a.size()) {
        if (j >= b.size() || a[i] < b[j]) {
            result.push_back(a[i]);
            ++i;
        } else if (a[i] == b[j]) {
            ++i;
            ++j;
        } else {
            ++j;
        }
    }
    
    return result;
}

std::vector<QueryTerm> BooleanSearch::parse_query(const std::string& query) {
    std::vector<QueryTerm> terms;
    auto tokens = tokenizer_.tokenize(query);
    
    QueryOperator next_op = QueryOperator::AND;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string token = tokens[i].text;
        
        if (token == "and" || token == "AND" || token == "и") {
            next_op = QueryOperator::AND;
            continue;
        }
        if (token == "or" || token == "OR" || token == "или") {
            next_op = QueryOperator::OR;
            continue;
        }
        if (token == "not" || token == "NOT" || token == "не") {
            next_op = QueryOperator::NOT;
            continue;
        }
        
        std::string stemmed = stemmer_.stem(token);
        terms.push_back(QueryTerm(stemmed, next_op));
        next_op = QueryOperator::AND;
    }
    
    return terms;
}

std::vector<SearchResult> BooleanSearch::search(const std::string& query, size_t max_results) {
    std::vector<SearchResult> results;
    
    std::vector<QueryTerm> query_terms = parse_query(query);
    if (query_terms.empty()) {
        return results;
    }
    
    std::vector<size_t> result_docs;
    bool first = true;
    
    for (size_t i = 0; i < query_terms.size(); ++i) {
        const QueryTerm& qt = query_terms[i];
        const PostingList* pl = index_.get_posting_list(qt.term);
        
        std::vector<size_t> term_docs;
        if (pl) {
            for (size_t j = 0; j < pl->postings.size(); ++j) {
                term_docs.push_back(pl->postings[j].doc_id);
            }
        }
        
        for (size_t k = 1; k < term_docs.size(); ++k) {
            size_t key = term_docs[k];
            size_t j = k;
            while (j > 0 && term_docs[j - 1] > key) {
                term_docs[j] = term_docs[j - 1];
                --j;
            }
            term_docs[j] = key;
        }
        
        if (first) {
            if (qt.op == QueryOperator::NOT) {
                continue;
            }
            result_docs = term_docs;
            first = false;
        } else {
            switch (qt.op) {
                case QueryOperator::AND:
                    result_docs = intersect(result_docs, term_docs);
                    break;
                case QueryOperator::OR:
                    result_docs = unite(result_docs, term_docs);
                    break;
                case QueryOperator::NOT:
                    result_docs = subtract(result_docs, term_docs);
                    break;
            }
        }
    }
    
    std::vector<size_t> doc_scores;
    doc_scores.reserve(result_docs.size());
    
    for (size_t i = 0; i < result_docs.size(); ++i) {
        size_t doc_id = result_docs[i];
        size_t score = 0;
        
        for (size_t j = 0; j < query_terms.size(); ++j) {
            if (query_terms[j].op == QueryOperator::NOT) continue;
            
            const PostingList* pl = index_.get_posting_list(query_terms[j].term);
            if (pl) {
                for (size_t k = 0; k < pl->postings.size(); ++k) {
                    if (pl->postings[k].doc_id == doc_id) {
                        score += pl->postings[k].frequency;
                        break;
                    }
                }
            }
        }
        doc_scores.push_back(score);
    }
    
    for (size_t i = 0; i < result_docs.size() && results.size() < max_results; ++i) {
        std::string doc_str = index_.get_doc_id(result_docs[i]);
        results.push_back(SearchResult(doc_str, doc_scores[i]));
    }
    
    for (size_t i = 1; i < results.size(); ++i) {
        SearchResult key = results[i];
        size_t j = i;
        while (j > 0 && results[j - 1].score < key.score) {
            results[j] = results[j - 1];
            --j;
        }
        results[j] = key;
    }
    
    return results;
}
