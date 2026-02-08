#include "json_reader.h"
#include <iostream>

std::string NdjsonReader::unescape_json_string(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            switch (next) {
                case 'n': result += '\n'; ++i; break;
                case 'r': result += '\r'; ++i; break;
                case 't': result += '\t'; ++i; break;
                case '"': result += '"'; ++i; break;
                case '\\': result += '\\'; ++i; break;
                case '/': result += '/'; ++i; break;
                case 'u':
                    if (i + 5 < str.size()) {
                        result += '?';
                        i += 5;
                    }
                    break;
                default:
                    result += str[i];
            }
        } else {
            result += str[i];
        }
    }
    
    return result;
}

std::string NdjsonReader::extract_field(const std::string& json, const std::string& field) {
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    
    if (pos == std::string::npos) {
        search = "\"" + field + "\": ";
        pos = json.find(search);
    }
    
    if (pos == std::string::npos) {
        return "";
    }
    
    pos += search.size();
    
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        ++pos;
    }
    
    if (pos >= json.size()) return "";
    
    if (json[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                value += json[pos];
                value += json[pos + 1];
                pos += 2;
            } else {
                value += json[pos];
                ++pos;
            }
        }
        return unescape_json_string(value);
    }
    
    if (json[pos] >= '0' && json[pos] <= '9') {
        std::string value;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            value += json[pos];
            ++pos;
        }
        return value;
    }
    
    return "";
}

std::vector<Document> NdjsonReader::load(const std::string& filename) {
    std::vector<Document> documents;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return documents;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        Document doc;
        doc.url = extract_field(line, "url");
        doc.title = extract_field(line, "title");
        doc.text = extract_field(line, "text");
        
        if (!doc.url.empty() && !doc.text.empty()) {
            documents.push_back(doc);
        }
    }
    
    file.close();
    return documents;
}
