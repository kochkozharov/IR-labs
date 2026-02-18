#include <iostream>
#include <chrono>
#include <string>
#include <sstream>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <cstdint>
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
static size_t g_total_tokens = 0;

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

// ---- Binary dump helpers ----

static void write_u64(std::ofstream& f, uint64_t v) {
    f.write(reinterpret_cast<const char*>(&v), 8);
}

static uint64_t read_u64(std::ifstream& f) {
    uint64_t v = 0;
    f.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

static void write_str(std::ofstream& f, const std::string& s) {
    write_u64(f, s.size());
    if (!s.empty()) f.write(s.data(), s.size());
}

static std::string read_str(std::ifstream& f) {
    uint64_t len = read_u64(f);
    if (len == 0) return "";
    std::string s(len, '\0');
    f.read(&s[0], len);
    return s;
}

bool is_dump_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    char magic[8] = {};
    f.read(magic, 8);
    return std::string(magic, 8) == "IRDUMP01";
}

bool save_dump(const std::string& path) {
    log_msg("INFO", "Saving index dump to: " + path);
    auto t0 = std::chrono::high_resolution_clock::now();

    std::ofstream f(path, std::ios::binary);
    if (!f.good()) {
        log_msg("ERROR", "Cannot open dump file for writing: " + path);
        return false;
    }

    f.write("IRDUMP01", 8);

    // Section 1: Full documents
    write_u64(f, g_documents.size());
    for (size_t i = 0; i < g_documents.size(); ++i) {
        write_str(f, g_documents[i].url);
        write_str(f, g_documents[i].title);
        write_str(f, g_documents[i].text);
    }

    // Section 2: Index document names
    const auto& idx_docs = g_index.documents();
    write_u64(f, idx_docs.size());
    for (size_t i = 0; i < idx_docs.size(); ++i)
        write_str(f, idx_docs[i]);

    // Section 3: Index terms + posting lists
    write_u64(f, g_index.vocabulary_size());
    g_index.for_each_term([&f](const std::string& term, const PostingList& pl) {
        write_str(f, term);
        write_u64(f, pl.postings.size());
        for (size_t i = 0; i < pl.postings.size(); ++i) {
            write_u64(f, pl.postings[i].doc_id);
            write_u64(f, pl.postings[i].frequency);
        }
    });

    // Section 4: Zipf data
    write_u64(f, g_zipf.total_terms());
    write_u64(f, g_zipf.unique_terms());
    g_zipf.for_each_term_count([&f](const std::string& term, size_t count) {
        write_str(f, term);
        write_u64(f, count);
    });

    // Section 5: Metadata
    write_u64(f, g_total_tokens);
    uint64_t time_ms = static_cast<uint64_t>(g_index_time * 1000);
    write_u64(f, time_ms);

    f.write("IREND000", 8);
    f.close();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    size_t dump_bytes = file_size_bytes(path);
    log_msg("INFO", "Dump saved: " + std::to_string(dump_bytes / 1024 / 1024) + " MB in " +
            std::to_string(ms / 1000.0) + "s");
    return true;
}

bool load_dump(const std::string& path) {
    log_msg("INFO", "Loading index dump from: " + path);
    auto t0 = std::chrono::high_resolution_clock::now();

    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        log_msg("ERROR", "Cannot open dump file: " + path);
        return false;
    }

    char magic[8] = {};
    f.read(magic, 8);
    if (std::string(magic, 8) != "IRDUMP01") {
        log_msg("ERROR", "Invalid dump file format");
        return false;
    }

    // Section 1: Full documents
    uint64_t num_docs = read_u64(f);
    g_documents.clear();
    g_documents.reserve(num_docs);
    for (uint64_t i = 0; i < num_docs; ++i) {
        Document doc;
        doc.url = read_str(f);
        doc.title = read_str(f);
        doc.text = read_str(f);
        g_documents.push_back(std::move(doc));
    }
    log_msg("INFO", "Loaded " + std::to_string(g_documents.size()) + " documents");

    // Build doc lookup
    g_doc_lookup.build(g_documents);

    // Section 2: Index document names
    g_index.clear();
    uint64_t num_idx_docs = read_u64(f);
    for (uint64_t i = 0; i < num_idx_docs; ++i)
        g_index.add_document_name(read_str(f));

    // Section 3: Index terms + posting lists
    uint64_t num_terms = read_u64(f);
    g_index.reserve_vocabulary(num_terms);
    for (uint64_t i = 0; i < num_terms; ++i) {
        std::string term = read_str(f);
        uint64_t num_postings = read_u64(f);
        PostingList pl;
        pl.postings.reserve(num_postings);
        for (uint64_t j = 0; j < num_postings; ++j) {
            uint64_t doc_id = read_u64(f);
            uint64_t freq = read_u64(f);
            pl.postings.push_back(Posting(doc_id, freq));
        }
        g_index.insert_posting_list(term, pl);
    }
    log_msg("INFO", "Loaded " + std::to_string(g_index.vocabulary_size()) + " terms");

    // Section 4: Zipf data
    g_zipf.clear();
    uint64_t total_terms = read_u64(f);
    uint64_t unique_terms = read_u64(f);
    g_zipf.set_total_terms(total_terms);
    g_zipf.reserve(unique_terms);
    for (uint64_t i = 0; i < unique_terms; ++i) {
        std::string term = read_str(f);
        uint64_t count = read_u64(f);
        g_zipf.insert_term_count(term, count);
    }

    // Section 5: Metadata
    g_total_tokens = read_u64(f);
    uint64_t time_ms = read_u64(f);
    g_index_time = time_ms / 1000.0;

    f.close();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    log_msg("INFO", "Dump loaded in " + std::to_string(load_ms / 1000.0) + "s");
    log_msg("INFO", "Documents: " + std::to_string(g_documents.size()));
    log_msg("INFO", "Vocabulary: " + std::to_string(g_index.vocabulary_size()));
    log_msg("INFO", "Total tokens: " + std::to_string(g_total_tokens));
    return true;
}

void build_index(const std::string& input_file) {
    log_msg("INFO", "============================================================");
    log_msg("INFO", "SEARCH ENGINE - Starting up");
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
    g_total_tokens = 0;
    
    for (size_t i = 0; i < g_documents.size(); ++i) {
        const auto& doc = g_documents[i];
        auto tokens = tokenizer.tokenize(doc.text);
        g_total_tokens += tokens.size();
        
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
                    + ", tokens so far: " + std::to_string(g_total_tokens)
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
    log_msg("INFO", "Total tokens:       " + std::to_string(g_total_tokens));
    log_msg("INFO", "Processing time:    " + std::to_string(g_index_time) + " seconds");
    log_msg("INFO", "Speed:              " + std::to_string((int)(g_documents.size() / g_index_time)) + " docs/sec");
    
    g_zipf.print_stats();
    std::cout.flush();
    
    log_msg("INFO", "Index built in memory, ready to serve");
}

void print_cli_help() {
    std::cout << "\nCommands:\n"
              << "  <query>           Search (supports &&, ||, !, parentheses)\n"
              << "  :stats            Show index statistics\n"
              << "  :zipf [N]         Show top N terms (default 20)\n"
              << "  :dump [path]      Save index dump\n"
              << "  :help             Show this help\n"
              << "  :quit             Exit\n\n"
              << "Examples:\n"
              << "  роман && поэзия\n"
              << "  литература || поэзия\n"
              << "  роман && !детектив\n"
              << "  (проза || поэзия) && автор\n"
              << std::endl;
}

void run_cli(const std::string& dump_path) {
    BooleanSearch search(g_index);
    
    std::cout << "\nSearch engine ready. " << g_index.document_count()
              << " documents, " << g_index.vocabulary_size() << " terms.\n";
    print_cli_help();
    
    std::string user_query;
    while (true) {
        std::cout << "> ";
        std::cout.flush();
        if (!std::getline(std::cin, user_query)) break;
        
        if (user_query.empty()) continue;
        if (user_query == ":quit" || user_query == ":exit" || user_query == "quit" || user_query == "exit") break;

        if (user_query == ":help") {
            print_cli_help();
            continue;
        }

        if (user_query == ":stats") {
            std::cout << "\n=== Index Statistics ===\n"
                      << "Documents:     " << g_index.document_count() << "\n"
                      << "Vocabulary:    " << g_index.vocabulary_size() << "\n"
                      << "Total tokens:  " << g_total_tokens << "\n"
                      << "Unique terms:  " << g_zipf.unique_terms() << "\n"
                      << "Index time:    " << std::fixed << std::setprecision(1) << g_index_time << "s\n"
                      << std::endl;
            continue;
        }

        if (user_query.substr(0, 5) == ":zipf") {
            int n = 20;
            if (user_query.size() > 5) {
                std::string arg = user_query.substr(6);
                if (!arg.empty()) n = std::stoi(arg);
            }
            auto terms = g_zipf.get_sorted_terms();
            size_t count = terms.size() < static_cast<size_t>(n) ? terms.size() : static_cast<size_t>(n);
            std::cout << "\nTop " << count << " terms:\n";
            for (size_t i = 0; i < count; ++i) {
                std::cout << "  " << std::setw(5) << (i + 1) << ". "
                          << std::setw(20) << std::left << terms[i].term
                          << std::right << " " << terms[i].frequency << "\n";
            }
            std::cout << std::endl;
            continue;
        }

        if (user_query.substr(0, 5) == ":dump") {
            std::string path = dump_path;
            if (user_query.size() > 6) {
                path = user_query.substr(6);
                while (!path.empty() && path[0] == ' ') path = path.substr(1);
            }
            save_dump(path);
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        auto results = search.search(user_query, 50);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto search_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        
        std::cout << "\nFound " << results.size() << " results ("
                  << std::fixed << std::setprecision(1) << search_us / 1000.0 << " ms):\n" << std::endl;
        
        size_t show = results.size() < 10 ? results.size() : 10;
        for (size_t i = 0; i < show; ++i) {
            const Document* doc = g_doc_lookup.find(results[i].doc_id);
            std::string title = doc ? doc->title : "";
            std::cout << "  " << (i + 1) << ". " << title << "\n"
                      << "     " << results[i].doc_id << "\n"
                      << "     TF-IDF: " << std::fixed << std::setprecision(2) << results[i].score << "\n"
                      << std::endl;
        }
        if (results.size() > show)
            std::cout << "  ... and " << (results.size() - show) << " more results\n" << std::endl;
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
        
        if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
        if (req.has_param("page")) page = std::stoi(req.get_param_value("page"));
        
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
        json << std::fixed << std::setprecision(2);
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
        
        std::ostringstream json;
        json << "{\"documents\":" << g_index.document_count()
             << ",\"vocabulary\":" << g_index.vocabulary_size()
             << ",\"total_terms\":" << g_zipf.total_terms()
             << ",\"unique_terms\":" << g_zipf.unique_terms()
             << ",\"index_time\":" << std::fixed << std::setprecision(1) << g_index_time
             << ",\"status\":\"ready\"}";
        
        res.set_content(json.str(), "application/json");
    });
    
    svr.Get("/api/zipf", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        
        int limit = 5000;
        if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
        
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

    svr.Post("/api/dump", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        bool ok = save_dump("/app/data/index.dump");
        if (ok) res.set_content("{\"status\":\"ok\"}", "application/json");
        else { res.status = 500; res.set_content("{\"error\":\"dump failed\"}", "application/json"); }
    });
    
    log_msg("INFO", "============================================================");
    log_msg("INFO", "HTTP SERVER READY");
    log_msg("INFO", "============================================================");
    log_msg("INFO", "Listening on 0.0.0.0:" + std::to_string(port));
    log_msg("INFO", "Endpoints:");
    log_msg("INFO", "  GET  /api/search?q=...&page=1&limit=50");
    log_msg("INFO", "  GET  /api/stats");
    log_msg("INFO", "  GET  /api/zipf?limit=5000");
    log_msg("INFO", "  GET  /api/document?url=...");
    log_msg("INFO", "  POST /api/dump");
    log_msg("INFO", "------------------------------------------------------------");
    
    svr.listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    std::string input_file = "/app/data/corpus.ndjson";
    std::string dump_path = "/app/data/index.dump";
    
    bool serve_mode = false;
    bool force_rebuild = false;
    int port = 9090;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--serve") {
            serve_mode = true;
        } else if (arg == "--rebuild") {
            force_rebuild = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--input" && i + 1 < argc) {
            input_file = argv[++i];
        } else if (arg == "--dump" && i + 1 < argc) {
            dump_path = argv[++i];
        }
    }
    
    log_msg("INFO", "Mode: " + std::string(serve_mode ? "HTTP server" : "CLI"));
    log_msg("INFO", "Input: " + input_file);
    log_msg("INFO", "Dump:  " + dump_path);
    
    bool loaded = false;

    if (!force_rebuild && file_exists(dump_path) && is_dump_file(dump_path)) {
        loaded = load_dump(dump_path);
        if (!loaded) log_msg("WARN", "Failed to load dump, falling back to corpus");
    }

    if (!loaded) {
        build_index(input_file);
        if (!g_documents.empty()) {
            save_dump(dump_path);
        }
    }
    
    if (g_documents.empty()) {
        log_msg("FATAL", "No documents loaded, exiting");
        return 1;
    }
    
    if (serve_mode) {
        run_server(port);
    } else {
        run_cli(dump_path);
    }
    
    return 0;
}
