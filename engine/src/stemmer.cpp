#include "stemmer.h"
#include <cstring>

bool PorterStemmer::is_vowel(const std::string& word, size_t pos) {
    if (pos + 1 >= word.size()) return false;
    
    unsigned char c1 = word[pos];
    unsigned char c2 = word[pos + 1];
    
    if (c1 == 0xD0) {
        return c2 == 0xB0 || c2 == 0xB5 || c2 == 0xB8 || 
               c2 == 0xBE || c2 == 0xB8;
    }
    if (c1 == 0xD1) {
        return c2 == 0x83 || c2 == 0x8B || c2 == 0x8D || 
               c2 == 0x8E || c2 == 0x8F || c2 == 0x91;
    }
    
    return false;
}

size_t PorterStemmer::get_rv_position(const std::string& word) {
    for (size_t i = 0; i + 1 < word.size(); i += 2) {
        if (is_vowel(word, i)) {
            return i + 2;
        }
    }
    return word.size();
}

bool PorterStemmer::ends_with(const std::string& word, const std::string& suffix) {
    if (suffix.size() > word.size()) return false;
    return word.compare(word.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string PorterStemmer::remove_suffix(const std::string& word, size_t suffix_len) {
    return word.substr(0, word.size() - suffix_len);
}

bool PorterStemmer::step1(std::string& word, size_t rv) {
    const char* perfective[] = {
        "\xD0\xB8\xD0\xB2\xD1\x88\xD0\xB8\xD1\x81\xD1\x8C",
        "\xD1\x8B\xD0\xB2\xD1\x88\xD0\xB8\xD1\x81\xD1\x8C",
        "\xD0\xB2\xD1\x88\xD0\xB8\xD1\x81\xD1\x8C",
        "\xD0\xB8\xD0\xB2\xD1\x88\xD0\xB8",
        "\xD1\x8B\xD0\xB2\xD1\x88\xD0\xB8",
        "\xD0\xB2\xD1\x88\xD0\xB8",
        "\xD0\xB8\xD0\xB2",
        "\xD1\x8B\xD0\xB2",
        "\xD0\xB2",
        nullptr
    };
    
    const char* reflexive[] = {
        "\xD1\x81\xD1\x8F",
        "\xD1\x81\xD1\x8C",
        nullptr
    };
    
    const char* adjective[] = {
        "\xD0\xB8\xD0\xBC\xD0\xB8",
        "\xD1\x8B\xD0\xBC\xD0\xB8",
        "\xD0\xB5\xD0\xB3\xD0\xBE",
        "\xD0\xBE\xD0\xB3\xD0\xBE",
        "\xD0\xB5\xD0\xBC\xD1\x83",
        "\xD0\xBE\xD0\xBC\xD1\x83",
        "\xD0\xB5\xD0\xB5",
        "\xD0\xB8\xD0\xB5",
        "\xD1\x8B\xD0\xB5",
        "\xD0\xBE\xD0\xB5",
        "\xD0\xB5\xD0\xB9",
        "\xD0\xB8\xD0\xB9",
        "\xD1\x8B\xD0\xB9",
        "\xD0\xBE\xD0\xB9",
        "\xD0\xB5\xD0\xBC",
        "\xD0\xB8\xD0\xBC",
        "\xD1\x8B\xD0\xBC",
        "\xD0\xBE\xD0\xBC",
        "\xD0\xB8\xD1\x85",
        "\xD1\x8B\xD1\x85",
        "\xD1\x83\xD1\x8E",
        "\xD1\x8E\xD1\x8E",
        "\xD0\xB0\xD1\x8F",
        "\xD1\x8F\xD1\x8F",
        "\xD0\xBE\xD1\x8E",
        "\xD0\xB5\xD1\x8E",
        nullptr
    };
    
    const char* noun[] = {
        "\xD0\xB8\xD1\x8F\xD0\xBC\xD0\xB8",
        "\xD1\x8F\xD0\xBC\xD0\xB8",
        "\xD0\xB0\xD0\xBC\xD0\xB8",
        "\xD0\xB8\xD0\xB5\xD0\xB9",
        "\xD0\xB8\xD1\x8F\xD0\xBC",
        "\xD0\xB8\xD0\xB5\xD0\xBC",
        "\xD0\xB8\xD1\x8F\xD1\x85",
        "\xD0\xBE\xD0\xB2",
        "\xD0\xB5\xD0\xB2",
        "\xD0\xB5\xD0\xB9",
        "\xD0\xBE\xD0\xB9",
        "\xD0\xB8\xD0\xB9",
        "\xD1\x8F\xD0\xBC",
        "\xD0\xB5\xD0\xBC",
        "\xD0\xB0\xD0\xBC",
        "\xD0\xBE\xD0\xBC",
        "\xD0\xB0\xD1\x85",
        "\xD1\x8F\xD1\x85",
        "\xD0\xB8\xD1\x8E",
        "\xD1\x8C\xD1\x8E",
        "\xD1\x8C\xD1\x8F",
        "\xD1\x8C\xD0\xB5",
        "\xD0\xB8\xD0\xB8",
        "\xD0\xB8",
        "\xD1\x8B",
        "\xD1\x83",
        "\xD0\xBE",
        "\xD0\xB9",
        "\xD0\xB0",
        "\xD0\xB5",
        "\xD1\x8F",
        "\xD1\x8C",
        nullptr
    };
    
    const char* verb[] = {
        "\xD0\xB5\xD0\xB9\xD1\x82\xD0\xB5",
        "\xD1\x83\xD0\xB9\xD1\x82\xD0\xB5",
        "\xD0\xB8\xD1\x82\xD0\xB5",
        "\xD0\xB9\xD1\x82\xD0\xB5",
        "\xD0\xB5\xD1\x88\xD1\x8C",
        "\xD0\xB5\xD1\x82\xD0\xB5",
        "\xD1\x83\xD1\x8E\xD1\x82",
        "\xD1\x8E\xD1\x82",
        "\xD0\xB0\xD1\x82",
        "\xD1\x8F\xD1\x82",
        "\xD0\xBD\xD1\x8B",
        "\xD0\xB5\xD0\xBD",
        "\xD1\x82\xD1\x8C",
        "\xD0\xB8\xD1\x88\xD1\x8C",
        "\xD1\x83\xD1\x8E",
        "\xD1\x8E",
        "\xD0\xBB\xD0\xB0",
        "\xD0\xBD\xD0\xB0",
        "\xD0\xBB\xD0\xB8",
        "\xD0\xBB\xD0\xBE",
        "\xD0\xBD\xD0\xBE",
        "\xD0\xB5\xD1\x82",
        "\xD0\xB9",
        "\xD0\xBB",
        "\xD0\xBD",
        nullptr
    };
    
    for (int i = 0; perfective[i]; ++i) {
        std::string suf = perfective[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            return true;
        }
    }
    
    for (int i = 0; reflexive[i]; ++i) {
        std::string suf = reflexive[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            break;
        }
    }
    
    bool found = false;
    for (int i = 0; adjective[i]; ++i) {
        std::string suf = adjective[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            found = true;
            break;
        }
    }
    
    if (found) return true;
    
    for (int i = 0; verb[i]; ++i) {
        std::string suf = verb[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            return true;
        }
    }
    
    for (int i = 0; noun[i]; ++i) {
        std::string suf = noun[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            return true;
        }
    }
    
    return false;
}

bool PorterStemmer::step2(std::string& word, size_t rv) {
    std::string suf = "\xD0\xB8";
    if (word.size() > rv + suf.size() && ends_with(word, suf)) {
        word = remove_suffix(word, suf.size());
        return true;
    }
    return false;
}

bool PorterStemmer::step3(std::string& word, size_t rv) {
    const char* derivational[] = {
        "\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C",
        "\xD0\xBE\xD1\x81\xD1\x82",
        nullptr
    };
    
    for (int i = 0; derivational[i]; ++i) {
        std::string suf = derivational[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            return true;
        }
    }
    return false;
}

bool PorterStemmer::step4(std::string& word, size_t rv) {
    std::string nn = "\xD0\xBD\xD0\xBD";
    if (word.size() > rv + nn.size() && ends_with(word, nn)) {
        word = remove_suffix(word, 2);
        return true;
    }
    
    const char* superlative[] = {
        "\xD0\xB5\xD0\xB9\xD1\x88\xD0\xB5",
        "\xD0\xB5\xD0\xB9\xD1\x88",
        nullptr
    };
    
    for (int i = 0; superlative[i]; ++i) {
        std::string suf = superlative[i];
        if (word.size() > rv + suf.size() && ends_with(word, suf)) {
            word = remove_suffix(word, suf.size());
            if (word.size() > rv + nn.size() && ends_with(word, nn)) {
                word = remove_suffix(word, 2);
            }
            return true;
        }
    }
    
    std::string soft = "\xD1\x8C";
    if (word.size() > rv + soft.size() && ends_with(word, soft)) {
        word = remove_suffix(word, soft.size());
        return true;
    }
    
    return false;
}

std::string PorterStemmer::stem(const std::string& word) {
    if (word.size() < 4) {
        return word;
    }
    
    std::string result = word;
    size_t rv = get_rv_position(result);
    
    step1(result, rv);
    step2(result, rv);
    step3(result, rv);
    step4(result, rv);
    
    return result;
}
