#include "tokenizer.h"

bool Tokenizer::is_cyrillic(unsigned char c1, unsigned char c2) {
    if (c1 == 0xD0) {
        return (c2 >= 0x90 && c2 <= 0xBF) || c2 == 0x81;
    }
    if (c1 == 0xD1) {
        return (c2 >= 0x80 && c2 <= 0x8F) || c2 == 0x91;
    }
    return false;
}

bool Tokenizer::is_letter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string Tokenizer::to_lower(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        
        if (c >= 'A' && c <= 'Z') {
            result += (c + 32);
        } else if (c == 0xD0 && i + 1 < str.size()) {
            unsigned char c2 = str[i + 1];
            if (c2 >= 0x90 && c2 <= 0x9F) {
                result += static_cast<char>(0xD0);
                result += static_cast<char>(c2 + 0x20);
                ++i;
            } else if (c2 >= 0xA0 && c2 <= 0xAF) {
                result += static_cast<char>(0xD1);
                result += static_cast<char>(c2 - 0x20);
                ++i;
            } else if (c2 == 0x81) {
                result += static_cast<char>(0xD1);
                result += static_cast<char>(0x91);
                ++i;
            } else {
                result += c;
            }
        } else {
            result += c;
        }
    }
    
    return result;
}

bool Tokenizer::is_valid_token(const std::string& token) {
    if (token.empty()) return false;
    
    size_t char_count = 0;
    bool has_letter = false;
    
    for (size_t i = 0; i < token.size(); ++i) {
        unsigned char c = token[i];
        if ((c & 0x80) == 0) {
            ++char_count;
            if (is_letter(c)) has_letter = true;
        } else if ((c & 0xE0) == 0xC0) {
            ++char_count;
            has_letter = true;
            ++i;
        } else if ((c & 0xF0) == 0xE0) {
            ++char_count;
            i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            ++char_count;
            i += 3;
        }
    }
    
    return char_count >= 2 && has_letter;
}

std::vector<Token> Tokenizer::tokenize(const std::string& text) {
    std::vector<Token> tokens;
    std::string current_token;
    size_t token_start = 0;
    
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        
        bool is_word_char = false;
        size_t char_len = 1;
        
        if ((c & 0x80) == 0) {
            is_word_char = is_letter(c) || (c >= '0' && c <= '9') || c == '-';
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            unsigned char c2 = text[i + 1];
            is_word_char = is_cyrillic(c, c2);
            char_len = 2;
        }
        
        if (is_word_char) {
            if (current_token.empty()) {
                token_start = i;
            }
            for (size_t j = 0; j < char_len && i + j < text.size(); ++j) {
                current_token += text[i + j];
            }
            i += char_len - 1;
        } else {
            if (!current_token.empty()) {
                std::string lower_token = to_lower(current_token);
                if (is_valid_token(lower_token)) {
                    tokens.push_back({lower_token, token_start});
                }
                current_token.clear();
            }
        }
    }
    
    if (!current_token.empty()) {
        std::string lower_token = to_lower(current_token);
        if (is_valid_token(lower_token)) {
            tokens.push_back({lower_token, token_start});
        }
    }
    
    return tokens;
}
