#ifndef JSON_READER_H
#define JSON_READER_H

#include <string>
#include <vector>
#include <fstream>

struct Document {
    std::string url;
    std::string title;
    std::string text;
};

class NdjsonReader {
public:
    static std::vector<Document> load(const std::string& filename);
    static std::string extract_field(const std::string& json, const std::string& field);
    static std::string unescape_json_string(const std::string& str);
};

#endif
