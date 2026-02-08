#include <iostream>
#include <chrono>
#include <string>
#include <sstream>
#include <cmath>
#include <ctime>
#include <fstream>
#include "httplib.h"
#include "json_reader.h"
#include "tokenizer.h"
#include "stemmer.h"
#include "inverted_index.h"
#include "boolean_search.h"
#include "zipf_analyzer.h"

static std::vector<Document> g_documents;
static InvertedIndex g_index;
static ZipfAnalyzer g_zipf;
static double g_index_time = 0;

struct DocLookup {
    StringMap<size_t> url_to_idx;
    
    void build(const std::vector<Document>& docs) {
        for (size_t i = 0; i < docs.size(); ++i) {
            url_to_idx.insert(docs[i].url, i);
        }
    }
    
    const Document* find(const std::string& url) const {
        const size_t* idx = url_to_idx.find(url);
        if (idx) return &g_documents[*idx];
        return nullptr;
    }
};

static DocLookup g_doc_lookup;

void log_msg(const std::string& level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    struct tm tm_buf;
    localtime_r(&time_t, &tm_buf);
    
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
    
    std::cout << "[" << time_str << "." 
              << (ms.count() / 100) << (ms.count() / 10 % 10) << (ms.count() % 10)
              << "] [" << level << "] " << msg << std::endl;
    std::cout.flush();
}

std::string escape_json_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string make_snippet(const std::string& text, const std::string& query, size_t ctx = 200) {
    Tokenizer tok;
    auto qtokens = tok.tokenize(query);
    
    std::string text_lower = text;
    for (size_t i = 0; i < text_lower.size(); ++i) {
        if (text_lower[i] >= 'A' && text_lower[i] <= 'Z')
            text_lower[i] += 32;
    }
    
    size_t best_pos = 0;
    for (size_t i = 0; i < qtokens.size(); ++i) {
        size_t pos = text_lower.find(qtokens[i].text);
        if (pos != std::string::npos) {
            best_pos = pos;
            break;
        }
    }
    
    size_t start = best_pos > ctx / 2 ? best_pos - ctx / 2 : 0;
    size_t end = best_pos + ctx < text.size() ? best_pos + ctx : text.size();
    
    std::string snippet;
    if (start > 0) snippet += "...";
    snippet += text.substr(start, end - start);
    if (end < text.size()) snippet += "...";
    
    return snippet;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

size_t file_size_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.good()) return 0;
    return f.tellg();
}

void build_index(const std::string& input_file) {
    log_msg("INFO", "============================================================");
    log_msg("INFO", "LINGUISTICS SEARCH ENGINE - Starting up");
    log_msg("INFO", "============================================================");
    
    log_msg("INFO", "Input file:  " + input_file);
    
    if (!file_exists(input_file)) {
        log_msg("ERROR", "Input file does not exist: " + input_file);
        log_msg("ERROR", "Make sure scraper has been run first: docker-compose up scraper");
        return;
    }
    
    size_t corpus_bytes = file_size_bytes(input_file);
    log_msg("INFO", "Corpus file size: " + std::to_string(corpus_bytes / 1024 / 1024) + " MB (" + std::to_string(corpus_bytes) + " bytes)");
    
    log_msg("INFO", "Loading documents from: " + input_file);
    auto load_start = std::chrono::high_resolution_clock::now();
    
    g_documents = NdjsonReader::load(input_file);
    
    auto load_end = std::chrono::high_resolution_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start).count();
    
    if (g_documents.empty()) {
        log_msg("ERROR", "No documents loaded! File might be empty or malformed.");
        return;
    }
    
    log_msg("INFO", "Loaded " + std::to_string(g_documents.size()) + " documents in " + std::to_string(load_ms / 1000.0) + "s");
    
    log_msg("INFO", "First document: " + g_documents[0].title + " (" + g_documents[0].url + ")");
    log_msg("INFO", "First doc text length: " + std::to_string(g_documents[0].text.size()) + " chars");
    
    log_msg("INFO", "Building document lookup table...");
    g_doc_lookup.build(g_documents);
    log_msg("INFO", "Lookup table ready");
    
    Tokenizer tokenizer;
    PorterStemmer stemmer;
    
    log_msg("INFO", "------------------------------------------------------------");
    log_msg("INFO", "Starting indexing pipeline...");
    log_msg("INFO", "------------------------------------------------------------");
    
    auto start_time = std::chrono::high_resolution_clock::now();
    size_t total_tokens = 0;
    
    for (size_t i = 0; i < g_documents.size(); ++i) {
        const auto& doc = g_documents[i];
        auto tokens = tokenizer.tokenize(doc.text);
        total_tokens += tokens.size();
        
        std::vector<std::string> stemmed_terms;
        for (size_t j = 0; j < tokens.size(); ++j) {
            std::string stem = stemmer.stem(tokens[j].text);
            stemmed_terms.push_back(stem);
            g_zipf.add_term(stem);
        }
        
        g_index.add_document(doc.url, stemmed_terms);
        
        if ((i + 1) % 500 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            double speed = (i + 1) * 1000.0 / elapsed;
            double eta = (g_documents.size() - i - 1) / speed;
            
            log_msg("INFO", "Indexed " + std::to_string(i + 1) + "/" + std::to_string(g_documents.size())
                    + " docs (" + std::to_string((int)speed) + " docs/s"
                    + ", ETA: " + std::to_string((int)eta) + "s"
                    + ", tokens so far: " + std::to_string(total_tokens)
                    + ", vocab: " + std::to_string(g_index.vocabulary_size()) + ")");
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    g_index_time = duration.count() / 1000.0;
    
    log_msg("INFO", "============================================================");
    log_msg("INFO", "INDEXING COMPLETE");
    log_msg("INFO", "============================================================");
    log_msg("INFO", "Documents indexed:  " + std::to_string(g_index.document_count()));
    log_msg("INFO", "Vocabulary size:    " + std::to_string(g_index.vocabulary_size()));
    log_msg("INFO", "Total tokens:       " + std::to_string(total_tokens));
    log_msg("INFO", "Processing time:    " + std::to_string(g_index_time) + " seconds");
    log_msg("INFO", "Speed:              " + std::to_string((int)(g_documents.size() / g_index_time)) + " docs/sec");
    
    g_zipf.print_stats();
    std::cout.flush();
    
    log_msg("INFO", "Index built in memory, ready to serve");
}

void run_cli() {
    BooleanSearch search(g_index);
    
    std::string test_queries[] = {
        "язык",
        "грамматика",
        "фонетика",
        "синтаксис AND морфология",
        "лингвистика OR языкознание",
        "слово NOT предложение"
    };
    
    log_msg("INFO", "Running test queries...");
    
    for (const auto& query : test_queries) {
        auto results = search.search(query, 5);
        std::cout << "Query: " << query << " -> " << results.size() << " results";
        if (results.size() > 0) {
            std::cout << " [top: " << results[0].doc_id << " score=" << results[0].score << "]";
        }
        std::cout << std::endl;
        std::cout.flush();
    }
    
    std::cout << "\nInteractive mode (enter 'quit' to exit):\n" << std::endl;
    std::cout.flush();
    
    std::string user_query;
    while (true) {
        std::cout << "> ";
        std::cout.flush();
        std::getline(std::cin, user_query);
        
        if (user_query == "quit" || user_query == "exit" || user_query.empty()) {
            break;
        }
        
        auto t0 = std::chrono::high_resolution_clock::now();
        auto results = search.search(user_query, 10);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto search_ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        
        std::cout << "Found " << results.size() << " results (" << search_ms / 1000.0 << " ms):\n" << std::endl;
        
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << (i + 1) << ". " << results[i].doc_id
                      << " (score: " << results[i].score << ")" << std::endl;
        }
        std::cout << std::endl;
        std::cout.flush();
    }
}

void run_server(int port) {
    httplib::Server svr;
    BooleanSearch search(g_index);
    
    svr.Get("/api/search", [&search](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        std::string query = req.get_param_value("q");
        int limit = 50;
        int page = 1;
        
        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
        }
        if (req.has_param("page")) {
            page = std::stoi(req.get_param_value("page"));
        }
        
        int per_page = 10;
        
        if (query.empty()) {
            res.set_content("{\"results\":[],\"total\":0,\"page\":1,\"pages\":0}", "application/json");
            return;
        }
        
        auto t0 = std::chrono::high_resolution_clock::now();
        auto results = search.search(query, limit);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto search_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        
        log_msg("QUERY", "\"" + query + "\" -> " + std::to_string(results.size()) + " results in " + std::to_string(search_us / 1000.0) + "ms");
        
        int total = results.size();
        int pages = (total + per_page - 1) / per_page;
        int start = (page - 1) * per_page;
        int end = start + per_page;
        if (end > total) end = total;
        if (start > total) start = total;
        
        std::ostringstream json;
        json << "{\"results\":[";
        
        for (int i = start; i < end; ++i) {
            if (i > start) json << ",";
            
            std::string title;
            std::string snippet;
            const Document* doc = g_doc_lookup.find(results[i].doc_id);
            if (doc) {
                title = doc->title;
                snippet = make_snippet(doc->text, query);
            }
            
            json << "{\"url\":\"" << escape_json_str(results[i].doc_id)
                 << "\",\"title\":\"" << escape_json_str(title)
                 << "\",\"score\":" << results[i].score
                 << ",\"snippet\":\"" << escape_json_str(snippet)
                 << "\"}";
        }
        
        json << "],\"total\":" << total
             << ",\"page\":" << page
             << ",\"pages\":" << pages << "}";
        
        res.set_content(json.str(), "application/json");
    });
    
    svr.Get("/api/stats", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        log_msg("HTTP", "GET /api/stats");
        
        std::ostringstream json;
        json << "{\"documents\":" << g_index.document_count()
             << ",\"vocabulary\":" << g_index.vocabulary_size()
             << ",\"total_terms\":" << g_zipf.total_terms()
             << ",\"unique_terms\":" << g_zipf.unique_terms()
             << ",\"index_time\":" << g_index_time
             << ",\"status\":\"ready\"}";
        
        res.set_content(json.str(), "application/json");
    });
    
    svr.Get("/api/zipf", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        int limit = 5000;
        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
        }
        
        log_msg("HTTP", "GET /api/zipf?limit=" + std::to_string(limit));
        
        auto terms = g_zipf.get_sorted_terms();
        size_t max_freq = terms.empty() ? 1 : terms[0].frequency;
        size_t count = terms.size() < (size_t)limit ? terms.size() : (size_t)limit;
        
        std::ostringstream json;
        json << "{\"total_unique\":" << terms.size()
             << ",\"total_terms\":" << g_zipf.total_terms()
             << ",\"data\":[";
        
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) json << ",";
            size_t rank = i + 1;
            double zipf_pred = static_cast<double>(max_freq) / rank;
            
            json << "{\"rank\":" << rank
                 << ",\"term\":\"" << escape_json_str(terms[i].term)
                 << "\",\"frequency\":" << terms[i].frequency
                 << ",\"log_rank\":" << std::log10(static_cast<double>(rank))
                 << ",\"log_frequency\":" << std::log10(static_cast<double>(terms[i].frequency))
                 << ",\"zipf_prediction\":" << zipf_pred << "}";
        }
        
        json << "]}";
        res.set_content(json.str(), "application/json");
    });
    
    svr.Get("/api/document", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        std::string url = req.get_param_value("url");
        log_msg("HTTP", "GET /api/document?url=" + url);
        
        const Document* doc = g_doc_lookup.find(url);
        
        if (doc) {
            std::ostringstream json;
            json << "{\"url\":\"" << escape_json_str(doc->url)
                 << "\",\"title\":\"" << escape_json_str(doc->title)
                 << "\",\"text\":\"" << escape_json_str(doc->text) << "\"}";
            res.set_content(json.str(), "application/json");
        } else {
            res.status = 404;
            res.set_content("{\"error\":\"not found\"}", "application/json");
        }
    });
    
    log_msg("INFO", "============================================================");
    log_msg("INFO", "HTTP SERVER READY");
    log_msg("INFO", "============================================================");
    log_msg("INFO", "Listening on 0.0.0.0:" + std::to_string(port));
    log_msg("INFO", "Endpoints:");
    log_msg("INFO", "  GET /api/search?q=...&page=1&limit=50");
    log_msg("INFO", "  GET /api/stats");
    log_msg("INFO", "  GET /api/zipf?limit=5000");
    log_msg("INFO", "  GET /api/document?url=...");
    log_msg("INFO", "------------------------------------------------------------");
    
    svr.listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    std::string input_file = "/app/data/corpus.ndjson";
    
    bool serve_mode = false;
    int port = 9090;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--serve") {
            serve_mode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--input" && i + 1 < argc) {
            input_file = argv[++i];
        }
    }
    
    log_msg("INFO", "Mode: " + std::string(serve_mode ? "HTTP server" : "CLI"));
    log_msg("INFO", "Input: " + input_file);
    
    build_index(input_file);
    
    if (g_documents.empty()) {
        log_msg("FATAL", "No documents loaded, exiting");
        return 1;
    }
    
    if (serve_mode) {
        run_server(port);
    } else {
        run_cli();
    }
    
    return 0;
}
