/**
 * tests/test_tokenizer.cpp
 *
 * Manual test for Tokenizer.
 * Run and verify output matches expected tokens.
 */
#include <iostream>

#include "tokenizer.hpp"

void test(const std::string& input, const std::vector<std::string>& expected) {
    Tokenizer t;
    auto tokens = t.tokenize(input);

    std::cout << "Input:    " << input << "\n";
    std::cout << "Tokens:   ";
    for (const auto& tok : tokens) std::cout << "[" << tok << "] ";
    std::cout << "\n";
    std::cout << "Expected: ";
    for (const auto& tok : expected) std::cout << "[" << tok << "] ";
    std::cout << "\n";
    std::cout << (tokens == expected ? "PASS" : "FAIL") << "\n\n";
}

int main() {
    test("docker run -p 3000:3000 myapp-api", {"docker", "run", "myapp", "api"});

    test("git status --short", {"git", "status", "short"});

    test("/home/shravani/projects/myApp", {"home", "shravani", "projects", "my", "app"});

    test("npm run build:production", {"npm", "run", "build", "production"});

    test("cd ~/histd/build", {"histd", "build"});

    return 0;
}