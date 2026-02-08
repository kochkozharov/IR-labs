#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <string>
#include <vector>
#include <cctype>

struct Token {
    std::string text;
    size_t position;
};

class Tokenizer {
public:
    std::vector<Token> tokenize(const std::string& text);

private:
    bool is_cyrillic(unsigned char c1, unsigned char c2);
    bool is_letter(unsigned char c);
    std::string to_lower(const std::string& str);
    bool is_valid_token(const std::string& token);
};

#endif
