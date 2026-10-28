#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#include <sstream>

namespace {

// ============================================================================
// 1. Error Handling
// ============================================================================

[[noreturn]] void compile_error(std::size_t line, const std::string& msg) {
    std::cerr << "Compile error at line " << line << ": " << msg << '\n';
    std::exit(43);
}

// ============================================================================
// 2. Lexer
// ============================================================================

enum class TokenType {
    END_OF_LINE,
    IDENTIFIER,
    INTEGER,
    PLUS,
    MINUS,
    STAR,
    LPAREN,
    RPAREN,
    EQUAL,
    KW_INPUT,
    KW_PRINT
};

struct Token {
    TokenType type;
    std::string text;
    std::uint8_t int_val = 0;
    std::size_t line = 0;
    std::size_t col = 0;
};

std::vector<Token> tokenize_line(const std::string& line_str, std::size_t line_num) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = line_str.size();

    while (i < n) {
        char c = line_str[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '#') {
            break; // comment extends to end of line
        }

        std::size_t col = i + 1;

        if (c == '+') {
            tokens.push_back({TokenType::PLUS, "+", 0, line_num, col});
            ++i;
        } else if (c == '-') {
            tokens.push_back({TokenType::MINUS, "-", 0, line_num, col});
            ++i;
        } else if (c == '*') {
            tokens.push_back({TokenType::STAR, "*", 0, line_num, col});
            ++i;
        } else if (c == '(') {
            tokens.push_back({TokenType::LPAREN, "(", 0, line_num, col});
            ++i;
        } else if (c == ')') {
            tokens.push_back({TokenType::RPAREN, ")", 0, line_num, col});
            ++i;
        } else if (c == '=') {
            tokens.push_back({TokenType::EQUAL, "=", 0, line_num, col});
            ++i;
        } else if (c >= '0' && c <= '9') {
            std::size_t start = i;
            while (i < n && line_str[i] >= '0' && line_str[i] <= '9') {
                ++i;
            }
            // Check maximal munch: cannot be immediately followed by letter or '_'
            if (i < n && ((line_str[i] >= 'a' && line_str[i] <= 'z') ||
                          (line_str[i] >= 'A' && line_str[i] <= 'Z') ||
                          line_str[i] == '_')) {
                compile_error(line_num, "invalid numeric identifier or token starting with digit");
            }
            if (i < n && line_str[i] == '.') {
                compile_error(line_num, "floating point constants are not supported");
            }
            std::string num_str = line_str.substr(start, i - start);
            // Verify within uint8 range (0..255)
            unsigned long long val = 0;
            for (char ch : num_str) {
                val = val * 10 + (ch - '0');
                if (val > 255) {
                    compile_error(line_num, "integer constant out of range 0..255: " + num_str);
                }
            }
            tokens.push_back({TokenType::INTEGER, num_str, static_cast<std::uint8_t>(val), line_num, col});
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            std::size_t start = i;
            while (i < n && ((line_str[i] >= 'a' && line_str[i] <= 'z') ||
                            (line_str[i] >= 'A' && line_str[i] <= 'Z') ||
                            (line_str[i] >= '0' && line_str[i] <= '9') ||
                            line_str[i] == '_')) {
                ++i;
            }
            std::string ident = line_str.substr(start, i - start);
            if (ident == "input") {
                tokens.push_back({TokenType::KW_INPUT, ident, 0, line_num, col});
            } else if (ident == "print") {
                tokens.push_back({TokenType::KW_PRINT, ident, 0, line_num, col});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, ident, 0, line_num, col});
            }
        } else {
            compile_error(line_num, std::string("unsupported character: '") + c + "'");
        }
    }
    tokens.push_back({TokenType::END_OF_LINE, "", 0, line_num, n + 1});
    return tokens;
}

// ============================================================================
// 3. AST & Parser
// ============================================================================

enum class ASTNodeType {
    INTEGER,
    VARIABLE,
    UNARY_PLUS,
    UNARY_MINUS,
    BINARY_ADD,
    BINARY_SUB,
    BINARY_MUL
};

struct ASTNode {
    ASTNodeType type;
    std::uint8_t int_val = 0;
    std::string var_name;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    std::size_t line = 0;
};

class LineParser {
public:
    LineParser(const std::vector<Token>& tokens, const std::set<std::string>& defined_vars)
        : tokens_(tokens), defined_vars_(defined_vars) {}

    std::unique_ptr<ASTNode> parse_expression() {
        return parse_additive(0);
    }

    void expect(TokenType type, const std::string& msg) {
        if (peek().type != type) {
            compile_error(peek().line, msg + " (got '" + peek().text + "')");
        }
        advance();
    }

    bool match(TokenType type) {
        if (peek().type == type) {
            advance();
            return true;
        }
        return false;
    }

    const Token& peek() const {
        return tokens_[pos_];
    }

    void advance() {
        if (pos_ < tokens_.size() - 1) {
            ++pos_;
        }
    }

    bool is_at_end() const {
        return tokens_[pos_].type == TokenType::END_OF_LINE;
    }

private:
    const std::vector<Token>& tokens_;
    const std::set<std::string>& defined_vars_;
    std::size_t pos_ = 0;

    std::unique_ptr<ASTNode> parse_additive(std::size_t depth) {
        if (depth > 500) compile_error(peek().line, "expression nesting depth exceeds 500");
        auto left = parse_multiplicative(depth + 1);
        while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
            Token op = peek();
            advance();
            auto right = parse_multiplicative(depth + 1);
            auto node = std::make_unique<ASTNode>();
            node->type = (op.type == TokenType::PLUS) ? ASTNodeType::BINARY_ADD : ASTNodeType::BINARY_SUB;
            node->left = std::move(left);
            node->right = std::move(right);
            node->line = op.line;
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<ASTNode> parse_multiplicative(std::size_t depth) {
        if (depth > 500) compile_error(peek().line, "expression nesting depth exceeds 500");
        auto left = parse_unary(depth + 1);
        while (peek().type == TokenType::STAR) {
            Token op = peek();
            advance();
            auto right = parse_unary(depth + 1);
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNodeType::BINARY_MUL;
            node->left = std::move(left);
            node->right = std::move(right);
            node->line = op.line;
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<ASTNode> parse_unary(std::size_t depth) {
        if (depth > 500) compile_error(peek().line, "expression nesting depth exceeds 500");
        if (peek().type == TokenType::PLUS) {
            Token op = peek();
            advance();
            auto operand = parse_unary(depth + 1);
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNodeType::UNARY_PLUS;
            node->left = std::move(operand);
            node->line = op.line;
            return node;
        }
        if (peek().type == TokenType::MINUS) {
            Token op = peek();
            advance();
            auto operand = parse_unary(depth + 1);
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNodeType::UNARY_MINUS;
            node->left = std::move(operand);
            node->line = op.line;
            return node;
        }
        return parse_primary(depth + 1);
    }

    std::unique_ptr<ASTNode> parse_primary(std::size_t depth) {
        if (depth > 500) compile_error(peek().line, "expression nesting depth exceeds 500");
        const Token& tok = peek();
        if (tok.type == TokenType::INTEGER) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNodeType::INTEGER;
            node->int_val = tok.int_val;
            node->line = tok.line;
            advance();
            return node;
        }
        if (tok.type == TokenType::IDENTIFIER) {
            if (defined_vars_.find(tok.text) == defined_vars_.end()) {
                compile_error(tok.line, "undefined variable '" + tok.text + "'");
            }
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNodeType::VARIABLE;
            node->var_name = tok.text;
            node->line = tok.line;
            advance();
            return node;
        }
        if (tok.type == TokenType::LPAREN) {
            advance();
            auto expr = parse_additive(depth + 1);
            if (peek().type != TokenType::RPAREN) {
                compile_error(peek().line, "expected ')'");
            }
            advance();
            return expr;
        }
        compile_error(tok.line, "unexpected token in expression: '" + tok.text + "'");
    }
};

// ============================================================================
// 4. Value Representation, Canonical Affine Combinations, and DAG
// ============================================================================

// A Value is represented as:
// const_part + sum(coef_i * base_i) mod 256.
// Each base_i is a BaseValue ID.
// BaseValue can be:
// 1. INPUT(idx)
// 2. MUL(base_u, base_v) where base_u <= base_v

struct BaseValue {
    bool is_input = false;
    int input_idx = 0;
    int mul_u = -1; // Value ID (not just base!)
    int mul_v = -1; // Value ID
};

struct AffineForm {
    std::uint8_t const_val = 0;
    std::map<int, std::uint8_t> terms; // base_id -> coefficient (1..255)

    bool is_constant() const {
        return terms.empty();
    }

    bool is_single_base(int& out_base) const {
        if (const_val == 0 && terms.size() == 1 && terms.begin()->second == 1) {
            out_base = terms.begin()->first;
            return true;
        }
        return false;
    }

    bool operator==(const AffineForm& other) const {
        return const_val == other.const_val && terms == other.terms;
    }

    bool operator<(const AffineForm& other) const {
        if (const_val != other.const_val) return const_val < other.const_val;
        return terms < other.terms;
    }
};

class ValueTable {
public:
    std::vector<BaseValue> base_values;
    std::map<std::pair<int, int>, int> mul_to_base; // (u_val, v_val) -> base_id
    std::vector<AffineForm> values;                 // value_id -> AffineForm
    std::map<AffineForm, int> form_to_value;

    int get_or_create_value(const AffineForm& form) {
        // Normalize terms: remove any zero coefficient
        AffineForm norm;
        norm.const_val = form.const_val;
        for (const auto& [base, coef] : form.terms) {
            std::uint8_t c = coef % 256;
            if (c != 0) {
                norm.terms[base] = c;
            }
        }
        auto it = form_to_value.find(norm);
        if (it != form_to_value.end()) {
            return it->second;
        }
        int id = static_cast<int>(values.size());
        values.push_back(norm);
        form_to_value[norm] = id;
        return id;
    }

    int make_constant(std::uint8_t c) {
        AffineForm f;
        f.const_val = c;
        return get_or_create_value(f);
    }

    int make_input(int input_idx) {
        BaseValue bv;
        bv.is_input = true;
        bv.input_idx = input_idx;
        int base_id = static_cast<int>(base_values.size());
        base_values.push_back(bv);

        AffineForm f;
        f.const_val = 0;
        f.terms[base_id] = 1;
        return get_or_create_value(f);
    }

    int add(int v1, int v2) {
        const AffineForm& a = values[v1];
        const AffineForm& b = values[v2];
        AffineForm res;
        res.const_val = static_cast<std::uint8_t>((a.const_val + b.const_val) % 256);
        res.terms = a.terms;
        for (const auto& [base, coef] : b.terms) {
            std::uint8_t c = (res.terms[base] + coef) % 256;
            if (c == 0) {
                res.terms.erase(base);
            } else {
                res.terms[base] = c;
            }
        }
        return get_or_create_value(res);
    }

    int sub(int v1, int v2) {
        const AffineForm& a = values[v1];
        const AffineForm& b = values[v2];
        AffineForm res;
        res.const_val = static_cast<std::uint8_t>((a.const_val - b.const_val + 256) % 256);
        res.terms = a.terms;
        for (const auto& [base, coef] : b.terms) {
            std::uint8_t c = (res.terms[base] - coef + 256) % 256;
            if (c == 0) {
                res.terms.erase(base);
            } else {
                res.terms[base] = c;
            }
        }
        return get_or_create_value(res);
    }

    int mul_const(int v, std::uint8_t k) {
        k %= 256;
        if (k == 0) return make_constant(0);
        if (k == 1) return v;
        const AffineForm& a = values[v];
        AffineForm res;
        res.const_val = static_cast<std::uint8_t>((a.const_val * k) % 256);
        for (const auto& [base, coef] : a.terms) {
            std::uint8_t c = static_cast<std::uint8_t>((coef * k) % 256);
            if (c != 0) {
                res.terms[base] = c;
            }
        }
        return get_or_create_value(res);
    }

    int mul(int v1, int v2) {
        const AffineForm& a = values[v1];
        const AffineForm& b = values[v2];
        if (a.is_constant()) {
            return mul_const(v2, a.const_val);
        }
        if (b.is_constant()) {
            return mul_const(v1, b.const_val);
        }

        // Canonical order of operands for CSE
        int u = v1, v = v2;
        if (u > v) std::swap(u, v);

        auto it = mul_to_base.find({u, v});
        int base_id = 0;
        if (it != mul_to_base.end()) {
            base_id = it->second;
        } else {
            BaseValue bv;
            bv.is_input = false;
            bv.mul_u = u;
            bv.mul_v = v;
            base_id = static_cast<int>(base_values.size());
            base_values.push_back(bv);
            mul_to_base[{u, v}] = base_id;
        }

        AffineForm res;
        res.const_val = 0;
        res.terms[base_id] = 1;
        return get_or_create_value(res);
    }

    int eval_ast(const ASTNode* node, const std::map<std::string, int>& env) {
        switch (node->type) {
            case ASTNodeType::INTEGER:
                return make_constant(node->int_val);
            case ASTNodeType::VARIABLE: {
                auto it = env.find(node->var_name);
                return it->second;
            }
            case ASTNodeType::UNARY_PLUS:
                return eval_ast(node->left.get(), env);
            case ASTNodeType::UNARY_MINUS: {
                int inner = eval_ast(node->left.get(), env);
                return sub(make_constant(0), inner);
            }
            case ASTNodeType::BINARY_ADD: {
                int l = eval_ast(node->left.get(), env);
                int r = eval_ast(node->right.get(), env);
                return add(l, r);
            }
            case ASTNodeType::BINARY_SUB: {
                int l = eval_ast(node->left.get(), env);
                int r = eval_ast(node->right.get(), env);
                return sub(l, r);
            }
            case ASTNodeType::BINARY_MUL: {
                int l = eval_ast(node->left.get(), env);
                int r = eval_ast(node->right.get(), env);
                return mul(l, r);
            }
        }
        return make_constant(0);
    }
};

// ============================================================================
// 5. High-Level Program Representation and Execution Plan
// ============================================================================

enum class StatementType {
    INPUT_STMT,
    PRINT_STMT
};

struct Statement {
    StatementType type;
    int input_idx = -1;
    int val_id = -1;
};

// ============================================================================
// 6. Brainfuck Code Generator & Optimizer
// ============================================================================

class BFGen {
public:
    int cur_ptr = 0;
    std::string code;
    std::vector<bool> is_zero;

    void ensure_cell(int cell) {
        if (cell >= static_cast<int>(is_zero.size())) {
            is_zero.resize(cell + 32, true); // cells start at 0
        }
    }

    void go(int target) {
        ensure_cell(target);
        if (target > cur_ptr) {
            code.append(target - cur_ptr, '>');
        } else if (target < cur_ptr) {
            code.append(cur_ptr - target, '<');
        }
        cur_ptr = target;
    }

    void clear(int cell) {
        ensure_cell(cell);
        if (!is_zero[cell]) {
            go(cell);
            code.append("[-]");
            is_zero[cell] = true;
        }
    }

    void inc(int cell, int k) {
        ensure_cell(cell);
        k = (k % 256 + 256) % 256;
        if (k == 0) return;
        go(cell);
        if (k <= 128) {
            code.append(k, '+');
        } else {
            code.append(256 - k, '-');
        }
        is_zero[cell] = false;
    }

    void input(int cell) {
        ensure_cell(cell);
        go(cell);
        code.push_back(',');
        is_zero[cell] = false;
    }

    void output(int cell) {
        ensure_cell(cell);
        go(cell);
        code.push_back('.');
    }

    // Move src into multiple destinations destructively (src becomes 0)
    void move_destruct(int src, const std::vector<std::pair<int, int>>& dests) {
        ensure_cell(src);
        if (is_zero[src]) return;
        go(src);
        code.push_back('[');
        code.push_back('-');
        for (const auto& [dest, coef] : dests) {
            inc(dest, coef);
        }
        go(src);
        code.push_back(']');
        is_zero[src] = true;
    }

    // Set constant value C into cell
    void set_constant(int cell, std::uint8_t c) {
        clear(cell);
        if (c == 0) return;
        // Direct plus/minus is simple and fast for small constants
        inc(cell, c);
    }
};

// ============================================================================
// 7. Compiler & Code Generation
// ============================================================================

class Compiler {
public:
    ValueTable vt;
    std::vector<Statement> stmts;
    std::set<std::string> defined_vars;

    void parse_program(std::istream& in) {
        std::string line;
        std::size_t line_num = 0;
        int next_input_idx = 0;
        std::map<std::string, int> env;

        while (std::getline(in, line)) {
            ++line_num;
            auto tokens = tokenize_line(line, line_num);
            if (tokens.size() <= 1) { // Only END_OF_LINE
                continue;
            }

            // Check statement form
            const Token& first = tokens[0];
            if (first.type == TokenType::KW_PRINT) {
                if (tokens.size() >= 2 && tokens[1].type == TokenType::EQUAL) {
                    compile_error(line_num, "reserved word 'print' cannot be used as a variable name");
                }
                // print(expression)
                if (tokens.size() < 4 || tokens[1].type != TokenType::LPAREN) {
                    compile_error(line_num, "expected '(' after print");
                }
                LineParser lp(tokens, defined_vars);
                lp.advance(); // skip 'print'
                lp.advance(); // skip '('
                auto expr = lp.parse_expression();
                lp.expect(TokenType::RPAREN, "expected ')'");
                if (!lp.is_at_end()) {
                    compile_error(line_num, "unexpected token after ')'");
                }
                int val_id = vt.eval_ast(expr.get(), env);
                stmts.push_back({StatementType::PRINT_STMT, -1, val_id});
            } else if (first.type == TokenType::IDENTIFIER) {
                // name = input() | expression
                if (tokens.size() < 3 || tokens[1].type != TokenType::EQUAL) {
                    compile_error(line_num, "expected '=' in assignment");
                }
                std::string var_name = first.text;
                if (tokens[2].type == TokenType::KW_INPUT) {
                    // input() call
                    if (tokens.size() != 6 || tokens[3].type != TokenType::LPAREN || tokens[4].type != TokenType::RPAREN || tokens[5].type != TokenType::END_OF_LINE) {
                        compile_error(line_num, "input() must be standalone and take no arguments");
                    }
                    int inp_id = next_input_idx++;
                    int val_id = vt.make_input(inp_id);
                    env[var_name] = val_id;
                    defined_vars.insert(var_name);
                    stmts.push_back({StatementType::INPUT_STMT, inp_id, val_id});
                } else {
                    // expression
                    LineParser lp(tokens, defined_vars);
                    lp.advance(); // skip identifier
                    lp.advance(); // skip '='
                    auto expr = lp.parse_expression();
                    if (!lp.is_at_end()) {
                        compile_error(line_num, "unexpected token after expression");
                    }
                    int val_id = vt.eval_ast(expr.get(), env);
                    env[var_name] = val_id;
                    defined_vars.insert(var_name);
                }
            } else if (first.type == TokenType::KW_INPUT) {
                if (tokens.size() >= 2 && tokens[1].type == TokenType::EQUAL) {
                    compile_error(line_num, "reserved word 'input' cannot be used as a variable name");
                }
                compile_error(line_num, "input() cannot be used as a standalone statement");
            } else {
                compile_error(line_num, "invalid statement: " + first.text);
            }
        }
    }

    std::string generate_brainfuck() {
        BFGen g;

        // 1. Dependency and Liveness Analysis
        // Find which base values and values are needed
        std::set<int> needed_values;
        std::set<int> needed_bases;

        for (const auto& stmt : stmts) {
            if (stmt.type == StatementType::PRINT_STMT) {
                needed_values.insert(stmt.val_id);
            }
        }

        // Recursively find needed base values
        std::vector<int> worklist(needed_values.begin(), needed_values.end());
        while (!worklist.empty()) {
            int vid = worklist.back();
            worklist.pop_back();
            const AffineForm& af = vt.values[vid];
            for (const auto& [base, coef] : af.terms) {
                if (needed_bases.insert(base).second) {
                    const BaseValue& bv = vt.base_values[base];
                    if (!bv.is_input) {
                        // Mul depends on bv.mul_u and bv.mul_v
                        if (needed_values.insert(bv.mul_u).second) {
                            worklist.push_back(bv.mul_u);
                        }
                        if (needed_values.insert(bv.mul_v).second) {
                            worklist.push_back(bv.mul_v);
                        }
                    }
                }
            }
        }

        // Base values allocation: map each needed base value to a cell
        // Also map needed inputs
        std::map<int, int> base_to_cell;
        int next_cell = 0;
        auto alloc_cell = [&]() {
            return next_cell++;
        };

        // Pre-allocate cells for inputs that are needed
        std::map<int, int> input_to_cell;
        for (std::size_t base = 0; base < vt.base_values.size(); ++base) {
            if (needed_bases.count(static_cast<int>(base))) {
                if (vt.base_values[base].is_input) {
                    int c = alloc_cell();
                    base_to_cell[static_cast<int>(base)] = c;
                    input_to_cell[vt.base_values[base].input_idx] = c;
                }
            }
        }

        int dummy_cell = alloc_cell(); // for dead inputs

        // Helper to materialize an AffineForm into a given destination cell
        // using existing base value cells
        auto materialize_affine = [&](int target_cell, const AffineForm& af) {
            g.set_constant(target_cell, af.const_val);
            for (const auto& [base, coef] : af.terms) {
                int b_cell = base_to_cell[base];
                // Non-destructive copy from b_cell with coefficient coef
                int T = alloc_cell();
                g.clear(T);
                g.move_destruct(b_cell, {{target_cell, coef}, {T, 1}});
                g.move_destruct(T, {{b_cell, 1}});
            }
        };

        // Nibble-based multiplication routine:
        // Computes u_cell * v_cell into dest_cell
        auto emit_nibble_multiply = [&](int u_cell, int v_cell, int dest_cell, bool is_square) {
            g.clear(dest_cell);

            // Allocate scratch cells
            int u_LO = alloc_cell();
            int u_HI = alloc_cell();
            int u_Y  = alloc_cell();
            int u_T  = alloc_cell();
            int u_W  = alloc_cell();

            // Extract u nibbles from a copy of u_cell
            int u_copy = alloc_cell();
            g.clear(u_copy);
            int T_copy = alloc_cell();
            g.clear(T_copy);
            g.move_destruct(u_cell, {{u_copy, 1}, {T_copy, 1}});
            g.move_destruct(T_copy, {{u_cell, 1}});

            // Extract nibbles of u_copy -> u_LO, u_HI
            auto extract = [&](int X, int LO, int HI, int Y, int T, int W) {
                g.clear(LO);
                g.clear(HI);
                g.clear(Y);
                g.clear(T);
                g.clear(W);
                g.move_destruct(X, {{Y, 16}, {T, 1}});

                g.go(Y);
                g.code.push_back('[');
                g.inc(Y, -16);
                g.inc(LO, 1);
                g.go(Y);
                g.code.push_back(']');
                g.is_zero[Y] = true;

                g.move_destruct(LO, {{T, -1}, {W, 1}});
                g.move_destruct(W, {{LO, 1}});

                g.go(T);
                g.code.push_back('[');
                g.inc(T, -16);
                g.inc(HI, 1);
                g.go(T);
                g.code.push_back(']');
                g.is_zero[T] = true;
            };

            extract(u_copy, u_LO, u_HI, u_Y, u_T, u_W);

            int T = alloc_cell();
            int TW = alloc_cell();
            g.clear(T);
            g.clear(TW);

            auto mulacc = [&](int P, int Q, int dest, int coef, int twin = -1) {
                g.go(P);
                g.code.push_back('[');
                g.inc(P, -1);
                if (twin != -1) {
                    g.inc(twin, 1);
                }
                g.move_destruct(Q, {{dest, coef}, {T, 1}});
                g.move_destruct(T, {{Q, 1}});
                g.go(P);
                g.code.push_back(']');
                g.is_zero[P] = true;
            };

            if (is_square) {
                // Square: dest += u_LO^2 + 32 * u_LO * u_HI
                int u_LO2 = alloc_cell();
                g.clear(u_LO2);
                g.move_destruct(u_LO, {{u_LO2, 1}, {TW, 1}});
                g.move_destruct(TW, {{u_LO, 1}});

                mulacc(u_LO2, u_LO, dest_cell, 1, TW);
                mulacc(TW, u_HI, dest_cell, 32);
            } else {
                // General multiply: extract v nibbles
                int v_LO = alloc_cell();
                int v_HI = alloc_cell();
                int v_Y  = alloc_cell();
                int v_T  = alloc_cell();
                int v_W  = alloc_cell();

                int v_copy = alloc_cell();
                g.clear(v_copy);
                int T2 = alloc_cell();
                g.clear(T2);
                g.move_destruct(v_cell, {{v_copy, 1}, {T2, 1}});
                g.move_destruct(T2, {{v_cell, 1}});

                extract(v_copy, v_LO, v_HI, v_Y, v_T, v_W);

                // dest += u_LO * v_LO + 16 * (u_LO * v_HI + u_HI * v_LO)
                mulacc(u_LO, v_LO, dest_cell, 1, TW);
                mulacc(TW, v_HI, dest_cell, 16);
                mulacc(u_HI, v_LO, dest_cell, 16);
            }
        };

        // Helper to materialize a BaseValue
        std::set<int> materialized_bases;
        auto ensure_base_materialized = [&](auto& self, int base_id) -> void {
            if (materialized_bases.count(base_id)) return;
            materialized_bases.insert(base_id);

            const BaseValue& bv = vt.base_values[base_id];
            if (bv.is_input) {
                // Already read via input
                return;
            }

            // Materialize operands bv.mul_u and bv.mul_v
            const AffineForm& af_u = vt.values[bv.mul_u];
            const AffineForm& af_v = vt.values[bv.mul_v];

            for (const auto& [b, c] : af_u.terms) self(self, b);
            for (const auto& [b, c] : af_v.terms) self(self, b);

            int u_cell = alloc_cell();
            int v_cell = alloc_cell();
            materialize_affine(u_cell, af_u);
            if (bv.mul_u == bv.mul_v) {
                v_cell = u_cell;
            } else {
                materialize_affine(v_cell, af_v);
            }

            int dest_cell = alloc_cell();
            base_to_cell[base_id] = dest_cell;
            emit_nibble_multiply(u_cell, v_cell, dest_cell, bv.mul_u == bv.mul_v);
        };

        // Execute statements in sequence
        for (const auto& stmt : stmts) {
            if (stmt.type == StatementType::INPUT_STMT) {
                auto it = input_to_cell.find(stmt.input_idx);
                if (it != input_to_cell.end()) {
                    g.input(it->second);
                } else {
                    g.input(dummy_cell); // dead input
                }
            } else if (stmt.type == StatementType::PRINT_STMT) {
                const AffineForm& af = vt.values[stmt.val_id];
                // Ensure all base dependencies are materialized
                for (const auto& [base, coef] : af.terms) {
                    ensure_base_materialized(ensure_base_materialized, base);
                }

                if (af.is_constant()) {
                    int c_cell = alloc_cell();
                    g.set_constant(c_cell, af.const_val);
                    g.output(c_cell);
                } else if (af.terms.size() == 1 && af.terms.begin()->second == 1) {
                    // Directly print base_cell + offset
                    int base = af.terms.begin()->first;
                    int b_cell = base_to_cell[base];
                    if (af.const_val == 0) {
                        g.output(b_cell);
                    } else {
                        g.inc(b_cell, af.const_val);
                        g.output(b_cell);
                        g.inc(b_cell, 256 - af.const_val); // restore
                    }
                } else {
                    // General affine combination
                    int out_cell = alloc_cell();
                    materialize_affine(out_cell, af);
                    g.output(out_cell);
                }
            }
        }

        g.code.push_back('\n');
        return g.code;
    }
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: exprc input.expr > output.bf\n";
        return 43;
    }
    std::ifstream source(argv[1]);
    if (!source) {
        std::cerr << "Error: cannot open source file\n";
        return 43;
    }

    try {
        Compiler compiler;
        compiler.parse_program(source);
        std::string bf = compiler.generate_brainfuck();
        std::cout << bf;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 43;
    }

    return 0;
}
