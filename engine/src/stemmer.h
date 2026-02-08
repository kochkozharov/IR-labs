#ifndef STEMMER_H
#define STEMMER_H

#include <string>

class PorterStemmer {
public:
    std::string stem(const std::string& word);

private:
    bool ends_with(const std::string& word, const std::string& suffix);
    std::string remove_suffix(const std::string& word, size_t suffix_len);
    size_t get_rv_position(const std::string& word);
    bool is_vowel(const std::string& word, size_t pos);
    
    bool step1(std::string& word, size_t rv);
    bool step2(std::string& word, size_t rv);
    bool step3(std::string& word, size_t rv);
    bool step4(std::string& word, size_t rv);
};

#endif
