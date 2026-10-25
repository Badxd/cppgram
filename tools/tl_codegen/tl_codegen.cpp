// cppgram TL code generator (C++ only, no Python).
//
// Usage: cppgram_tl_codegen <out_header> <out_impl_dir> <schema.tl>...
//
// Reads TL schema files (Telegram TL format, same as the Kurigram/Telegram schema)
// and writes:
//   - one header with all constructor/function structs, abstract type variants and generic templates
//   - api_impl_N.cpp files with out-of-line serialize/deserialize definitions
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Param {
    std::string name;
    std::string type;
    bool is_flags_def = false;
    std::optional<unsigned> flag_bit;
    std::string flags_field;
};

struct Decl {
    std::string name;
    std::uint32_t id = 0;
    std::vector<std::string> tparams;
    std::vector<Param> params;
    std::string result;
    bool is_function = false;
};

const std::set<std::string>& cpp_keywords() {
    static const std::set<std::string> kw = {
        "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "class",
        "compl", "const", "continue", "default", "delete", "do", "double", "else", "enum", "explicit", "export",
        "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
        "new", "not", "not_eq", "operator", "or", "or_eq", "private", "protected", "public", "register", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw", "true", "try",
        "typedef", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while", "xor", "xor_eq",
        "final", "override", "concept", "requires", "error", "read", "write", "min", "max"};
    return kw;
}

std::string ident(std::string s) {
    for (char& c : s) {
        if (c == '.') {
            c = '_';
        }
    }
    if (cpp_keywords().count(s) != 0) {
        s += '_';
    }
    return s;
}

bool is_builtin_type(const std::string& t) {
    static const std::set<std::string> builtins = {"int", "long", "double", "string", "bytes", "Bool",
                                                   "true", "int128", "int256", "Object", "#"};
    return builtins.count(t) != 0;
}

// Normalizes a TL type expression: builtins stay as they are, named types get C++-safe identifiers.
std::string normalize_type(const std::string& t) {
    if (is_builtin_type(t) || (!t.empty() && t[0] == '!')) {
        return t;
    }
    if (t.size() > 8 && t.compare(0, 7, "Vector<") == 0 && t.back() == '>') {
        return "Vector<" + normalize_type(t.substr(7, t.size() - 8)) + ">";
    }
    if (t.size() > 8 && t.compare(0, 7, "vector<") == 0 && t.back() == '>') {
        return "vector<" + normalize_type(t.substr(7, t.size() - 8)) + ">";
    }
    return ident(t);
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string tok;
    while (in >> tok) {
        out.push_back(tok);
    }
    return out;
}

Decl parse_decl(const std::string& raw) {
    std::string s = raw;
    if (const auto comment = s.find("//"); comment != std::string::npos) {
        s = s.substr(0, comment);
    }
    s = trim(s);
    if (s.empty() || s.back() != ';') {
        throw std::runtime_error("expected ';' at end of declaration: " + raw);
    }
    s.pop_back();
    s = trim(s);
    const auto eq = s.rfind(" = ");
    if (eq == std::string::npos) {
        throw std::runtime_error("missing ' = ' in declaration: " + raw);
    }
    Decl d;
    d.result = normalize_type(trim(s.substr(eq + 3)));
    const auto toks = split_ws(s.substr(0, eq));
    if (toks.empty()) {
        throw std::runtime_error("empty declaration: " + raw);
    }
    const auto hash = toks[0].find('#');
    if (hash == std::string::npos) {
        throw std::runtime_error("declaration without explicit id: " + raw);
    }
    d.name = ident(toks[0].substr(0, hash));
    d.id = static_cast<std::uint32_t>(std::stoul(toks[0].substr(hash + 1), nullptr, 16));
    for (std::size_t i = 1; i < toks.size(); ++i) {
        const std::string& t = toks[i];
        if (t.front() == '{') {
            const std::string inner = t.substr(1, t.size() - 2);
            d.tparams.push_back(inner.substr(0, inner.find(':')));
            continue;
        }
        const auto colon = t.find(':');
        if (colon == std::string::npos) {
            throw std::runtime_error("bad parameter '" + t + "' in: " + raw);
        }
        Param p;
        p.name = ident(t.substr(0, colon));
        const std::string type = t.substr(colon + 1);
        if (type == "#") {
            p.is_flags_def = true;
            p.type = "#";
        } else if (starts_with(type, "flags") && type.find('.') != std::string::npos && type.find('?') != std::string::npos) {
            const auto dot = type.find('.');
            const auto q = type.find('?');
            if (dot > q) {
                throw std::runtime_error("bad flags parameter '" + t + "' in: " + raw);
            }
            p.flags_field = type.substr(0, dot);
            p.flag_bit = static_cast<unsigned>(std::stoul(type.substr(dot + 1, q - dot - 1)));
            p.type = normalize_type(type.substr(q + 1));
        } else {
            p.type = normalize_type(type);
        }
        d.params.push_back(p);
    }
    return d;
}

class Generator {
public:
    void add_file(const fs::path& path) {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("cannot open " + path.string());
        }
        bool functions = false;
        std::string line;
        while (std::getline(in, line)) {
            const std::string s = trim(line);
            if (s == "---functions---") {
                functions = true;
                continue;
            }
            if (s == "---types---") {
                functions = false;
                continue;
            }
            if (s.empty() || starts_with(s, "//") || s.find(" = ") == std::string::npos) {
                continue;
            }
            Decl d = parse_decl(s);
            if (is_hand_parsed(d)) {
                continue;
            }
            d.is_function = functions;
            decls_.push_back(std::move(d));
        }
    }

    void build_index() {
        for (const Decl& d : decls_) {
            if (!d.is_function) {
                abstracts_[d.result].push_back(&d);
            }
        }
        for (const Decl& d : decls_) {
            if (d.is_function) {
                functions_.push_back(&d);
            } else {
                types_.push_back(&d);
            }
        }
        for (const Decl& d : decls_) {
            for (const Param& p : d.params) {
                validate_type(p.type, d, d.tparams);
            }
            if (d.is_function) {
                if (!is_generic_result(d)) {
                    validate_type(d.result, d, {});
                }
            }
        }
    }

    void write(const fs::path& header_path, const fs::path& impl_dir) {
        std::vector<std::string> impls;
        std::ostringstream h;
        h << "// Generated by cppgram_tl_codegen from the TL schema. Do not edit by hand.\n";
        h << "#pragma once\n\n#include <cstdint>\n#include <memory>\n#include <optional>\n#include <variant>\n#include <vector>\n\n";
        h << "#include \"cppgram/mtproto/tl.h\"\n\n";
        h << "namespace cppgram::mtproto::tl {\n\n";

        for (const auto& [name, ctors] : abstracts_) {
            (void)ctors;
            h << "struct " << name << ";\n";
        }
        h << "\nnamespace types {\n\n";
        for (const Decl* d : types_) {
            emit_struct_decl(h, *d, impls, false);
        }
        h << "}  // namespace types\n\nnamespace functions {\n\n";
        for (const Decl* d : functions_) {
            if (d->tparams.empty()) {
                emit_struct_decl(h, *d, impls, false);
            } else {
                emit_generic_function(h, *d);
            }
        }
        h << "}  // namespace functions\n\n";

        for (const auto& [name, ctors] : abstracts_) {
            h << "struct " << name << " {\n";
            h << "    std::variant<";
            for (std::size_t i = 0; i < ctors.size(); ++i) {
                h << (i ? ", " : "") << "types::" << ctors[i]->name;
            }
            h << "> value;\n";
            h << "    void serialize(TlWriter& tlw) const;\n";
            h << "    static " << name << " read(TlReader& r);\n";
            h << "};\n\n";
            emit_abstract_impl(impls, name, ctors);
        }
        h << "}  // namespace cppgram::mtproto::tl\n";

        fs::create_directories(header_path.parent_path());
        std::ofstream(header_path) << h.str();

        fs::create_directories(impl_dir);
        for (const auto& entry : fs::directory_iterator(impl_dir)) {
            if (entry.path().filename().string().rfind("api_impl_", 0) == 0) {
                fs::remove(entry.path());
            }
        }
        constexpr std::size_t kPerFile = 350;
        std::size_t file_index = 0;
        for (std::size_t start = 0; start < impls.size(); start += kPerFile, ++file_index) {
            std::ostringstream out;
            out << "// Generated by cppgram_tl_codegen. Do not edit by hand.\n";
            out << "#include \"cppgram/mtproto/schema/api.h\"\n\n";
            out << "namespace cppgram::mtproto::tl {\n\n";
            for (std::size_t i = start; i < impls.size() && i < start + kPerFile; ++i) {
                out << impls[i] << "\n";
            }
            out << "}  // namespace cppgram::mtproto::tl\n";
            std::ofstream(impl_dir / ("api_impl_" + std::to_string(file_index) + ".cpp")) << out.str();
        }
        std::cout << "types=" << types_.size() << " functions=" << functions_.size()
                  << " abstracts=" << abstracts_.size() << " impl_files=" << file_index << "\n";
    }

private:
    // Bare containers (no constructor id on the wire) are parsed by hand in the session code.
    static bool is_hand_parsed(const Decl& d) {
        static const std::set<std::string> names = {"future_salt", "future_salts", "get_future_salts"};
        static const std::set<std::string> results = {"FutureSalt", "FutureSalts"};
        return names.count(d.name) != 0 || results.count(d.result) != 0;
    }

    static bool is_builtin(const std::string& t) {
        static const std::set<std::string> builtins = {"int", "long", "double", "string", "bytes", "Bool",
                                                       "true", "int128", "int256", "Object"};
        return builtins.count(t) != 0;
    }

    static bool is_generic_result(const Decl& d) {
        return !d.tparams.empty() && d.result == d.tparams.front();
    }

    static bool is_vector(const std::string& t) {
        return starts_with(t, "Vector<") && t.back() == '>';
    }

    static bool is_bare_vector(const std::string& t) {
        return starts_with(t, "vector<") && t.back() == '>';
    }

    static std::string vector_inner(const std::string& t) {
        return t.substr(7, t.size() - 8);
    }

    void validate_type(const std::string& t, const Decl& d, const std::vector<std::string>& tparams) const {
        if (t == "#" || is_builtin(t)) {
            return;
        }
        if (starts_with(t, "!")) {
            return;
        }
        if (is_vector(t) || is_bare_vector(t)) {
            validate_type(vector_inner(t), d, tparams);
            return;
        }
        for (const auto& tp : tparams) {
            if (t == tp) {
                return;
            }
        }
        if (abstracts_.count(t) == 0) {
            throw std::runtime_error("unknown type '" + t + "' used in " + d.name);
        }
    }

    // C++ type for a TL type. `boxed` is true for fields and vector elements, which hold shared_ptr for named types.
    std::string cpp_type(const std::string& t, bool boxed) const {
        if (t == "int") return "std::int32_t";
        if (t == "long") return "std::int64_t";
        if (t == "double") return "double";
        if (t == "string") return "std::string";
        if (t == "bytes" || t == "int128" || t == "int256") return "Bytes";
        if (t == "Object") return "RawObject";
        if (t == "Bool" || t == "true") return "bool";
        if (is_vector(t) || is_bare_vector(t)) return "std::vector<" + cpp_type(vector_inner(t), true) + ">";
        if (starts_with(t, "!")) return t.substr(1);
        if (generic_names_.count(t) != 0) return t;
        return boxed ? "std::shared_ptr<tl::" + t + ">" : "tl::" + t;
    }

    std::string read_expr(const std::string& t, const std::string& rv, bool boxed) {
        if (t == "int") return rv + ".read_int32()";
        if (t == "long") return rv + ".read_int64()";
        if (t == "double") return rv + ".read_double()";
        if (t == "string") return rv + ".read_string()";
        if (t == "bytes") return rv + ".read_bytes()";
        if (t == "Bool") return rv + ".read_bool()";
        if (t == "int128") return rv + ".read_int128()";
        if (t == "int256") return rv + ".read_int256()";
        if (t == "Object") return "RawObject{" + rv + ".read_raw(" + rv + ".remaining())}";
        if (is_bare_vector(t)) {
            const std::string inner = vector_inner(t);
            const std::string elem = cpp_type(inner, true);
            const std::string var = "r" + std::to_string(counter_++);
            return "tl::read_bare_vector<" + elem + ">(" + rv + ", [&](TlReader& " + var + ") -> " + elem + " { return " +
                   read_expr(inner, var, true) + "; })";
        }
        if (is_vector(t)) {
            const std::string inner = vector_inner(t);
            const std::string elem = cpp_type(inner, true);
            const std::string var = "r" + std::to_string(counter_++);
            return "tl::read_vector<" + elem + ">(" + rv + ", [&](TlReader& " + var + ") -> " + elem + " { return " +
                   read_expr(inner, var, true) + "; })";
        }
        if (boxed) {
            return "tl::read_boxed<tl::" + t + ">(" + rv + ")";
        }
        return "tl::" + t + "::read(" + rv + ")";
    }

    std::string write_stmt(const std::string& t, const std::string& v, bool boxed, const std::string& wv) {
        static const std::map<std::string, std::string> methods = {
            {"int", "write_int32"},   {"long", "write_int64"},   {"double", "write_double"},
            {"string", "write_string"}, {"bytes", "write_bytes"}, {"Bool", "write_bool"},
            {"int128", "write_int128"}, {"int256", "write_int256"}};
        if (const auto it = methods.find(t); it != methods.end()) {
            return wv + "." + it->second + "(" + v + ");";
        }
        if (is_bare_vector(t)) {
            const std::string inner = vector_inner(t);
            const std::string wn = "tlw" + std::to_string(counter_);
            const std::string en = "e" + std::to_string(counter_);
            ++counter_;
            return "tl::write_bare_vector(" + wv + ", " + v + ", [&](TlWriter& " + wn + ", const auto& " + en + ") { " +
                   write_stmt(inner, en, true, wn) + " });";
        }
        if (is_vector(t)) {
            const std::string inner = vector_inner(t);
            const std::string wn = "tlw" + std::to_string(counter_);
            const std::string en = "e" + std::to_string(counter_);
            ++counter_;
            return "tl::write_vector(" + wv + ", " + v + ", [&](TlWriter& " + wn + ", const auto& " + en + ") { " +
                   write_stmt(inner, en, true, wn) + " });";
        }
        if (t == "Object" || starts_with(t, "!")) {
            return v + ".serialize(" + wv + ");";
        }
        if (boxed) {
            return v + "->serialize(" + wv + ");";
        }
        return v + ".serialize(" + wv + ");";
    }

    void emit_fields(std::ostringstream& os, const Decl& d, const std::string& indent) {
        for (const Param& p : d.params) {
            if (p.is_flags_def) {
                continue;
            }
            if (p.flag_bit && p.type == "true") {
                os << indent << "bool " << p.name << " = false;\n";
            } else if (p.flag_bit) {
                os << indent << "std::optional<" << cpp_type(p.type, true) << "> " << p.name << ";\n";
            } else {
                os << indent << cpp_type(p.type, true) << " " << p.name << "{};\n";
            }
        }
    }

    // Body of serialize(): writes the constructor id, then flags and fields in TL order.
    std::string serialize_body(const Decl& d, const std::string& indent) {
        std::ostringstream os;
        os << indent << "tlw.write_uint32(kId);\n";
        for (const Param& p : d.params) {
            if (p.is_flags_def) {
                os << indent << "{\n" << indent << "    std::uint32_t flags_v = 0;\n";
                for (const Param& f : d.params) {
                    if (!f.flag_bit || f.flags_field != p.name) {
                        continue;
                    }
                    const std::string cond = f.type == "true" ? f.name : f.name + ".has_value()";
                    os << indent << "    if (" << cond << ") flags_v |= (1u << " << *f.flag_bit << ");\n";
                }
                os << indent << "    tlw.write_uint32(flags_v);\n" << indent << "}\n";
                continue;
            }
            if (p.flag_bit) {
                if (p.type == "true") {
                    continue;
                }
                os << indent << "if (" << p.name << ".has_value()) { "
                   << write_stmt(p.type, p.name + ".value()", true, "tlw") << " }\n";
            } else {
                os << indent << write_stmt(p.type, p.name, true, "tlw") << "\n";
            }
        }
        return os.str();
    }

    // Body of read_body(): reads flags and fields in TL order. `qualified` is the struct name.
    std::string read_body_impl(const Decl& d, const std::string& qualified) {
        std::ostringstream os;
        os << qualified << " " << qualified << "::read_body(TlReader& rv) {\n";
        os << "    " << qualified << " out;\n";
        for (const Param& p : d.params) {
            if (p.is_flags_def) {
                os << "    std::uint32_t fl_" << p.name << " = rv.read_uint32();\n";
            }
        }
        for (const Param& p : d.params) {
            if (p.is_flags_def) {
                continue;
            }
            if (p.flag_bit) {
                const std::string cond = "(fl_" + p.flags_field + " & (1u << " + std::to_string(*p.flag_bit) + ")) != 0";
                if (p.type == "true") {
                    os << "    out." << p.name << " = " << cond << ";\n";
                } else {
                    os << "    if (" << cond << ") { out." << p.name << " = " << read_expr(p.type, "rv", true) << "; }\n";
                }
            } else {
                os << "    out." << p.name << " = " << read_expr(p.type, "rv", true) << ";\n";
            }
        }
        os << "    return out;\n}\n\n";
        return os.str();
    }

    void emit_struct_decl(std::ostringstream& h, const Decl& d, std::vector<std::string>& impls, bool /*tmpl*/) {
        const std::string ns = d.is_function ? "functions" : "types";
        generic_names_.clear();
        const std::string qualified = ns + "::" + d.name;
        if (!d.is_function) {
            h << "struct " << d.name << " {\n";
            h << "    static constexpr std::uint32_t kId = 0x" << hex(d.id) << "u;\n";
            std::ostringstream fields;
            emit_fields(fields, d, "    ");
            h << fields.str();
            h << "    void serialize(TlWriter& tlw) const;\n";
            h << "    static types::" << d.name << " read_body(TlReader& rv);\n";
            h << "};\n\n";
            impls.push_back("void " + qualified + "::serialize(TlWriter& tlw) const {\n" + serialize_body(d, "    ") +
                            "}\n\n" + read_body_impl(d, qualified));
            return;
        }
        h << "struct " << d.name << " {\n";
        h << "    static constexpr std::uint32_t kId = 0x" << hex(d.id) << "u;\n";
        std::ostringstream fields;
        emit_fields(fields, d, "    ");
        h << fields.str();
        h << "    using Result = " << cpp_type(d.result, false) << ";\n";
        h << "    void serialize(TlWriter& tlw) const;\n";
        h << "    static Result read_result(TlReader& rv);\n";
        h << "};\n\n";
        impls.push_back("void " + qualified + "::serialize(TlWriter& tlw) const {\n" + serialize_body(d, "    ") +
                        "}\n\n" + "functions::" + d.name + "::Result " + qualified + "::read_result(TlReader& rv) {\n" +
                        "    return " + read_expr(d.result, "rv", false) + ";\n}\n\n");
    }

    void emit_generic_function(std::ostringstream& h, const Decl& d) {
        generic_names_ = std::set<std::string>(d.tparams.begin(), d.tparams.end());
        h << "template <";
        for (std::size_t i = 0; i < d.tparams.size(); ++i) {
            h << (i ? ", " : "") << "class " << d.tparams[i];
        }
        h << ">\nstruct " << d.name << " {\n";
        h << "    static constexpr std::uint32_t kId = 0x" << hex(d.id) << "u;\n";
        std::ostringstream fields;
        emit_fields(fields, d, "    ");
        h << fields.str();
        h << "    using Result = typename " << d.tparams.front() << "::Result;\n";
        h << "    void serialize(TlWriter& tlw) const {\n" << serialize_body(d, "        ") << "    }\n";
        h << "    static Result read_result(TlReader& rv) { return " << d.tparams.front()
          << "::read_result(rv); }\n";
        h << "};\n\n";
        generic_names_.clear();
    }

    void emit_abstract_impl(std::vector<std::string>& impls, const std::string& name,
                            const std::vector<const Decl*>& ctors) {
        std::ostringstream os;
        os << "void " << name << "::serialize(TlWriter& tlw) const {\n";
        os << "    std::visit([&](const auto& v) { v.serialize(tlw); }, value);\n}\n\n";
        os << name << " " << name << "::read(TlReader& r) {\n";
        os << "    const std::uint32_t id = r.read_uint32();\n";
        os << "    switch (id) {\n";
        for (const Decl* c : ctors) {
            os << "        case 0x" << hex(c->id) << "u: return " << name << "{types::" << c->name
               << "::read_body(r)};\n";
        }
        os << "        default: throw TlError(\"unknown constructor id in " << name << "\");\n";
        os << "    }\n}\n\n";
        impls.push_back(os.str());
    }

    static std::string hex(std::uint32_t v) {
        std::ostringstream os;
        os << std::hex << std::nouppercase;
        os.width(8);
        os.fill('0');
        os << v;
        return os.str();
    }

    std::vector<Decl> decls_;
    std::vector<const Decl*> types_;
    std::vector<const Decl*> functions_;
    std::map<std::string, std::vector<const Decl*>> abstracts_;
    std::set<std::string> generic_names_;
    std::size_t counter_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: cppgram_tl_codegen <out_header> <out_impl_dir> <schema.tl>...\n";
        return 2;
    }
    try {
        Generator gen;
        for (int i = 3; i < argc; ++i) {
            gen.add_file(argv[i]);
        }
        gen.build_index();
        gen.write(argv[1], argv[2]);
    } catch (const std::exception& e) {
        std::cerr << "cppgram_tl_codegen: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
