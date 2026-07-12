/**
 * src/tokenizer.cpp
 *
 * Implementation of Tokenizer.
 * See tokenizer.hpp for pipeline documentation.
 */
#include "tokenizer.hpp"

#include <algorithm>  //std::transform
#include <cctype>     // std::isupper, std::islower, std::isdigit, std::tolower

Tokenizer::Tokenizer() {
    stop_words_ = {"the", "and", "or", "in", "at", "to", "for", "with", "from", "of", "a",
                   "an",  "is",  "it", "on", "by", "as", "be",  "this", "that", "are"};

    noise_commands_ = {"cd", "pwd"};
}

std::vector<std::string> Tokenizer::tokenize(const std::string& input) const {
    // step 1 - split on delimiters
    std::vector<std::string> parts = split(input);

    // step 2 - expand camelCase
    parts = expand_camel(parts);

    // step 3 - lowercase + filter
    std::vector<std::string> tokens;
    for (std::string& part : parts) {
        // lowercase
        std::transform(part.begin(), part.end(), part.begin(), ::tolower);

        // filter
        if (is_stop_word(part) || !is_valid_token(part)) continue;
        tokens.push_back(part);
    }

    return tokens;
}

std::vector<std::string> Tokenizer::split(const std::string& input) const {
    std::vector<std::string> parts;
    std::string current;

    for (char c : input) {
        // check if c is a delimiter
        if (c == ' ' || c == '-' || c == '/' || c == '.' || c == '_' || c == '=' || c == ':' ||
            c == ',' || c == '"' || c == '\'' || c == '@' || c == '(' || c == ')' || c == '[' ||
            c == ']') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    return parts;
}

std::vector<std::string> Tokenizer::expand_camel(const std::vector<std::string>& parts) const {
    std::vector<std::string> expanded;

    for (const std::string& part : parts) {
        std::string current;

        for (size_t i = 0; i < part.size(); i++) {
            char c = part[i];

            if (i > 0 && std::isupper(c) && std::islower(part[i - 1])) {
                if (!current.empty()) {
                    expanded.push_back(current);
                    current.clear();
                }
            }
            current += c;
        }

        if (!current.empty()) {
            expanded.push_back(current);
        }
    }

    return expanded;
}

bool Tokenizer::is_stop_word(const std::string& token) const {
    return stop_words_.find(token) != stop_words_.end();
}

bool Tokenizer::is_valid_token(const std::string& token) const {
    // too short
    if (token.size() < 2) return false;

    // starts with - (shell flag)
    if (token[0] == '-') return false;

    // common shell commands that add no search value
    if (noise_commands_.count(token) > 0) return false;

    // purely numeric - port numbers, PIDs, etc
    bool all_digits = true;
    for (char c : token) {
        if (!isdigit(c)) {
            all_digits = false;
            break;
        }
    }
    if (all_digits) return false;

    return true;
}