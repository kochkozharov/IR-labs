#include "boolean_search.h"
#include <cmath>

BooleanSearch::BooleanSearch(const InvertedIndex& index) : index_(index), qpos_(0) {}

std::vector<size_t> BooleanSearch::intersect(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { result.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) ++i;
        else ++j;
    }
    return result;
}

std::vector<size_t> BooleanSearch::unite(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { result.push_back(a[i]); ++i; ++j; }
        else if (a[i] < b[j]) { result.push_back(a[i]); ++i; }
        else { result.push_back(b[j]); ++j; }
    }
    while (i < a.size()) result.push_back(a[i++]);
    while (j < b.size()) result.push_back(b[j++]);
    return result;
}

std::vector<size_t> BooleanSearch::subtract(const std::vector<size_t>& a, const std::vector<size_t>& b) {
    std::vector<size_t> result;
    size_t i = 0, j = 0;
    while (i < a.size()) {
        if (j >= b.size() || a[i] < b[j]) { result.push_back(a[i]); ++i; }
        else if (a[i] == b[j]) { ++i; ++j; }
        else ++j;
    }
    return result;
}

std::vector<size_t> BooleanSearch::all_doc_ids() {
    std::vector<size_t> result;
    size_t n = index_.document_count();
    result.reserve(n);
    for (size_t i = 0; i < n; ++i)
        result.push_back(i);
    return result;
}

// ---- Lexer ----

std::vector<BooleanSearch::QToken> BooleanSearch::lex(const std::string& q) {
    std::vector<QToken> result;
    size_t i = 0;
    while (i < q.size()) {
        unsigned char c = q[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }

        if (c == '(' ) { result.push_back({TokType::LPAREN, ""}); ++i; continue; }
        if (c == ')' ) { result.push_back({TokType::RPAREN, ""}); ++i; continue; }
        if (c == '!' && (i + 1 >= q.size() || q[i+1] != '=')) {
            result.push_back({TokType::NOT_OP, ""}); ++i; continue;
        }
        if (c == '&' && i + 1 < q.size() && q[i+1] == '&') {
            result.push_back({TokType::AND_OP, ""}); i += 2; continue;
        }
        if (c == '|' && i + 1 < q.size() && q[i+1] == '|') {
            result.push_back({TokType::OR_OP, ""}); i += 2; continue;
        }

        std::string word;
        while (i < q.size()) {
            unsigned char ch = q[i];
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
                ch == '(' || ch == ')' || ch == '!') break;
            if (ch == '&' && i + 1 < q.size() && q[i+1] == '&') break;
            if (ch == '|' && i + 1 < q.size() && q[i+1] == '|') break;
            word += static_cast<char>(ch);
            ++i;
        }
        if (word.empty()) { ++i; continue; }

        // ASCII-lowercase for operator detection
        std::string lw;
        for (size_t k = 0; k < word.size(); ++k) {
            char ch = word[k];
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            lw += ch;
        }

        if (lw == "and") { result.push_back({TokType::AND_OP, ""}); continue; }
        if (lw == "or")  { result.push_back({TokType::OR_OP, ""});  continue; }
        if (lw == "not") { result.push_back({TokType::NOT_OP, ""}); continue; }

        // Cyrillic operator equivalents (UTF-8 byte comparison)
        if (word == "\xd0\xb8" || word == "\xd0\x98") {
            result.push_back({TokType::AND_OP, ""}); continue;
        }
        if (word == "\xd0\xb8\xd0\xbb\xd0\xb8" || word == "\xd0\x98\xd0\x9b\xd0\x98" ||
            word == "\xd0\x98\xd0\xbb\xd0\xb8") {
            result.push_back({TokType::OR_OP, ""}); continue;
        }
        if (word == "\xd0\xbd\xd0\xb5" || word == "\xd0\x9d\xd0\xb5" || word == "\xd0\x9d\xd0\x95") {
            result.push_back({TokType::NOT_OP, ""}); continue;
        }

        auto tokens = tokenizer_.tokenize(word);
        for (size_t t = 0; t < tokens.size(); ++t) {
            std::string stemmed = stemmer_.stem(tokens[t].text);
            result.push_back({TokType::WORD, stemmed});
        }
    }
    result.push_back({TokType::END, ""});
    return result;
}

// ---- Recursive descent parser ----

std::vector<size_t> BooleanSearch::term_docs(const std::string& stemmed) {
    const PostingList* pl = index_.get_posting_list(stemmed);
    if (!pl) return {};
    std::vector<size_t> docs;
    docs.reserve(pl->postings.size());
    for (size_t i = 0; i < pl->postings.size(); ++i)
        docs.push_back(pl->postings[i].doc_id);
    // Posting lists are already sorted by doc_id (insertion order = sequential)
    for (size_t i = 1; i < docs.size(); ++i) {
        size_t key = docs[i];
        size_t j = i;
        while (j > 0 && docs[j-1] > key) { docs[j] = docs[j-1]; --j; }
        docs[j] = key;
    }
    return docs;
}

// or_expr = and_expr ( "||" and_expr )*
std::vector<size_t> BooleanSearch::parse_or_expr() {
    auto result = parse_and_expr();
    while (qpos_ < qtokens_.size() && qtokens_[qpos_].type == TokType::OR_OP) {
        ++qpos_;
        result = unite(result, parse_and_expr());
    }
    return result;
}

// and_expr = unary ( ("&&" | implicit) unary )*
std::vector<size_t> BooleanSearch::parse_and_expr() {
    auto result = parse_unary();
    while (qpos_ < qtokens_.size()) {
        if (qtokens_[qpos_].type == TokType::AND_OP) {
            ++qpos_;
            result = intersect(result, parse_unary());
        } else if (qtokens_[qpos_].type == TokType::WORD ||
                   qtokens_[qpos_].type == TokType::NOT_OP ||
                   qtokens_[qpos_].type == TokType::LPAREN) {
            result = intersect(result, parse_unary());
        } else {
            break;
        }
    }
    return result;
}

// unary = "!" unary | primary
std::vector<size_t> BooleanSearch::parse_unary() {
    if (qpos_ < qtokens_.size() && qtokens_[qpos_].type == TokType::NOT_OP) {
        ++qpos_;
        return subtract(all_doc_ids(), parse_unary());
    }
    return parse_primary();
}

// primary = "(" or_expr ")" | WORD
std::vector<size_t> BooleanSearch::parse_primary() {
    if (qpos_ < qtokens_.size() && qtokens_[qpos_].type == TokType::LPAREN) {
        ++qpos_;
        auto result = parse_or_expr();
        if (qpos_ < qtokens_.size() && qtokens_[qpos_].type == TokType::RPAREN)
            ++qpos_;
        return result;
    }
    if (qpos_ < qtokens_.size() && qtokens_[qpos_].type == TokType::WORD) {
        std::string term = qtokens_[qpos_].text;
        ++qpos_;
        return term_docs(term);
    }
    return {};
}

// ---- Merge sort for results ----

static void merge_sr(std::vector<SearchResult>& a, std::vector<SearchResult>& t,
                     size_t l, size_t m, size_t r) {
    size_t i = l, j = m, k = l;
    while (i < m && j < r) {
        if (a[i].score >= a[j].score) t[k++] = a[i++];
        else t[k++] = a[j++];
    }
    while (i < m) t[k++] = a[i++];
    while (j < r) t[k++] = a[j++];
    for (size_t x = l; x < r; ++x) a[x] = t[x];
}

static void msort_sr(std::vector<SearchResult>& a, std::vector<SearchResult>& t,
                     size_t l, size_t r) {
    if (r - l <= 1) return;
    size_t m = l + (r - l) / 2;
    msort_sr(a, t, l, m);
    msort_sr(a, t, m, r);
    merge_sr(a, t, l, m, r);
}

// ---- Search with TF-IDF ----

std::vector<SearchResult> BooleanSearch::search(const std::string& query, size_t max_results) {
    qtokens_ = lex(query);
    qpos_ = 0;

    if (qtokens_.empty() || qtokens_[0].type == TokType::END)
        return {};

    auto result_docs = parse_or_expr();

    // Collect positive (non-negated) query terms for TF-IDF scoring
    std::vector<std::string> pos_terms;
    bool prev_not = false;
    for (size_t i = 0; i < qtokens_.size(); ++i) {
        if (qtokens_[i].type == TokType::NOT_OP) { prev_not = true; continue; }
        if (qtokens_[i].type == TokType::WORD) {
            if (!prev_not) pos_terms.push_back(qtokens_[i].text);
            prev_not = false;
        } else {
            prev_not = false;
        }
    }
    if (pos_terms.empty()) {
        for (size_t i = 0; i < qtokens_.size(); ++i)
            if (qtokens_[i].type == TokType::WORD)
                pos_terms.push_back(qtokens_[i].text);
    }

    size_t N = index_.document_count();

    // Precompute IDF for each positive term
    std::vector<double> idfs;
    idfs.reserve(pos_terms.size());
    for (size_t i = 0; i < pos_terms.size(); ++i) {
        const PostingList* pl = index_.get_posting_list(pos_terms[i]);
        double df = pl ? static_cast<double>(pl->postings.size()) : 0.0;
        idfs.push_back((df > 0 && N > 0) ? std::log10(static_cast<double>(N) / df) : 0.0);
    }

    // Score each result document
    std::vector<SearchResult> results;
    results.reserve(result_docs.size());

    for (size_t i = 0; i < result_docs.size(); ++i) {
        size_t doc_id = result_docs[i];
        double score = 0.0;
        for (size_t j = 0; j < pos_terms.size(); ++j) {
            const PostingList* pl = index_.get_posting_list(pos_terms[j]);
            if (!pl) continue;
            for (size_t k = 0; k < pl->postings.size(); ++k) {
                if (pl->postings[k].doc_id == doc_id) {
                    score += static_cast<double>(pl->postings[k].frequency) * idfs[j];
                    break;
                }
            }
        }
        results.push_back(SearchResult(index_.get_doc_id(doc_id), score));
    }

    // Merge sort by score descending
    if (results.size() > 1) {
        std::vector<SearchResult> tmp(results.size());
        msort_sr(results, tmp, 0, results.size());
    }

    if (results.size() > max_results)
        results.resize(max_results);

    return results;
}
