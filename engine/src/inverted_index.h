#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <string>
#include <vector>
#include "string_map.h"

struct Posting {
    size_t doc_id;
    size_t frequency;
    
    Posting() : doc_id(0), frequency(0) {}
    Posting(size_t d, size_t f) : doc_id(d), frequency(f) {}
};

class PostingList {
public:
    std::vector<Posting> postings;
    
    void add(size_t doc_id);
    void sort_by_doc_id();
};

class InvertedIndex {
private:
    StringMap<PostingList> index_;
    std::vector<std::string> documents_;
    
public:
    void add_document(const std::string& doc_id, const std::vector<std::string>& terms);
    PostingList* get_posting_list(const std::string& term);
    const PostingList* get_posting_list(const std::string& term) const;
    
    size_t get_doc_index(const std::string& doc_id);
    const std::string& get_doc_id(size_t index) const;
    
    size_t vocabulary_size() const;
    size_t document_count() const;
    
    template<typename Func>
    void for_each_term(Func func) const {
        index_.for_each(func);
    }
};

#endif
