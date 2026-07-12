/**
 * src/tokenizer.hpp
 *
 * Tokenizer — breaks raw command strings into searchable tokens
 * for the inverted index.
 *
 * Pipeline for each input string:
 *   1. Split on delimiters (space, dash, slash, dot, underscore, equals)
 *   2. Expand camelCase into component words
 *   3. Lowercase all tokens
 *   4. Filter stop words, short tokens, and pure numbers
 *
 * Snake_case is handled automatically in step 1 since underscore
 * is treated as a delimiter — no separate expansion needed.
 *
 * Used by the index builder to tokenize both the cmd and cwd fields
 * of each ParsedRecord before inserting into the InvertedIndex.
 */
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

class Tokenizer {
   public:
    /**
     * Constructor — initializes the stop words set.
     */
    Tokenizer();

    /**
     * Tokenizes a raw input string into clean, searchable tokens.
     * Applies the full pipeline: split → expand → lowercase → filter.
     *
     * @param input  raw string — command, path, or any text
     * @return       vector of clean lowercase tokens
     */
    std::vector<std::string> tokenize(const std::string& input) const;

   private:
    /**
     * Splits input on delimiter characters:
     * space, dash, slash, dot, underscore, equals, colon, comma.
     * Returns raw unsanitized parts — lowercase and filtering happen later.
     */
    std::vector<std::string> split(const std::string& input) const;

    /**
     * Expands camelCase tokens into component words.
     * "myAppConfig" → ["my", "App", "Config"]
     * Lowercasing happens after this step.
     */
    std::vector<std::string> expand_camel(const std::vector<std::string>& parts) const;

    /**
     * Returns true if the token is in the stop words set.
     * Stop words are common English words with no search value
     * in a shell history context.
     */
    bool is_stop_word(const std::string& token) const;

    /**
     * Returns true if the token is worth indexing.
     * Filters out:
     *   - tokens shorter than 2 characters
     *   - purely numeric tokens (e.g. port numbers like 3000, 8080)
     *   - tokens starting with - (shell flags like -v, --verbose)
     */
    bool is_valid_token(const std::string& token) const;

    // set of stop words initialized in constructor
    // using unordered_set for O(1) lookup per token
    std::unordered_set<std::string> stop_words_;

    // set of noise commands initialized in constructor
    std::unordered_set<std::string> noise_commands_;
};