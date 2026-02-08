#include "inverted_index.h"

void PostingList::add(size_t doc_id) {
    for (size_t i = 0; i < postings.size(); ++i) {
        if (postings[i].doc_id == doc_id) {
            postings[i].frequency++;
            return;
        }
    }
    postings.push_back(Posting(doc_id, 1));
}

void PostingList::sort_by_doc_id() {
    for (size_t i = 1; i < postings.size(); ++i) {
        Posting key = postings[i];
        size_t j = i;
        while (j > 0 && postings[j - 1].doc_id > key.doc_id) {
            postings[j] = postings[j - 1];
            --j;
        }
        postings[j] = key;
    }
}

size_t InvertedIndex::get_doc_index(const std::string& doc_id) {
    for (size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i] == doc_id) {
            return i;
        }
    }
    documents_.push_back(doc_id);
    return documents_.size() - 1;
}

void InvertedIndex::add_document(const std::string& doc_id, const std::vector<std::string>& terms) {
    size_t doc_index = get_doc_index(doc_id);
    
    for (size_t i = 0; i < terms.size(); ++i) {
        PostingList& pl = index_.get_or_create(terms[i]);
        pl.add(doc_index);
    }
}

PostingList* InvertedIndex::get_posting_list(const std::string& term) {
    return index_.find(term);
}

const PostingList* InvertedIndex::get_posting_list(const std::string& term) const {
    return index_.find(term);
}

const std::string& InvertedIndex::get_doc_id(size_t index) const {
    return documents_[index];
}

size_t InvertedIndex::vocabulary_size() const {
    return index_.size();
}

size_t InvertedIndex::document_count() const {
    return documents_.size();
}
