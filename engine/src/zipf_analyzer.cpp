#include "zipf_analyzer.h"
#include <iostream>
#include <cmath>

ZipfAnalyzer::ZipfAnalyzer() : total_terms_(0) {}

void ZipfAnalyzer::add_term(const std::string& term) {
    size_t* count = term_counts_.find(term);
    if (count) {
        (*count)++;
    } else {
        term_counts_.insert(term, 1);
    }
    ++total_terms_;
}

size_t ZipfAnalyzer::unique_terms() const {
    return term_counts_.size();
}

size_t ZipfAnalyzer::total_terms() const {
    return total_terms_;
}

static void merge(std::vector<TermFrequency>& arr, std::vector<TermFrequency>& tmp, size_t left, size_t mid, size_t right) {
    size_t i = left, j = mid, k = left;
    
    while (i < mid && j < right) {
        if (arr[i].frequency >= arr[j].frequency) {
            tmp[k++] = arr[i++];
        } else {
            tmp[k++] = arr[j++];
        }
    }
    while (i < mid) tmp[k++] = arr[i++];
    while (j < right) tmp[k++] = arr[j++];
    
    for (size_t x = left; x < right; ++x) {
        arr[x] = tmp[x];
    }
}

static void merge_sort(std::vector<TermFrequency>& arr, std::vector<TermFrequency>& tmp, size_t left, size_t right) {
    if (right - left <= 1) return;
    size_t mid = left + (right - left) / 2;
    merge_sort(arr, tmp, left, mid);
    merge_sort(arr, tmp, mid, right);
    merge(arr, tmp, left, mid, right);
}

static void sort_terms(std::vector<TermFrequency>& terms) {
    if (terms.size() <= 1) return;
    std::vector<TermFrequency> tmp(terms.size());
    merge_sort(terms, tmp, 0, terms.size());
}

std::vector<TermFrequency> ZipfAnalyzer::get_sorted_terms() const {
    std::vector<TermFrequency> terms;
    terms.reserve(term_counts_.size());
    
    term_counts_.for_each([&terms](const std::string& term, size_t freq) {
        terms.push_back(TermFrequency(term, freq));
    });
    
    sort_terms(terms);
    
    for (size_t i = 0; i < terms.size(); ++i) {
        terms[i].rank = i + 1;
    }
    
    return terms;
}

void ZipfAnalyzer::print_stats() {
    std::cout << "\n=== ZIPF ANALYSIS ===" << std::endl;
    std::cout << "Total terms: " << total_terms_ << std::endl;
    std::cout << "Unique terms: " << term_counts_.size() << std::endl;
    std::cout.flush();
    
    std::vector<TermFrequency> terms = get_sorted_terms();
    
    std::cout << "\nTop 20 terms:" << std::endl;
    for (size_t i = 0; i < 20 && i < terms.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << terms[i].term
                  << " - " << terms[i].frequency << std::endl;
    }
    std::cout.flush();
}
