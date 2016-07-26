#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace program_options {
using string_t = std::string;

namespace detail {
template <typename Target, typename Source, bool IsSame>
class lexical_cast_t {
   public:
    static Target cast(Source&& arg) {
        Target ret;
        std::stringstream ss;
        if (!(ss << std::forward<Source>(arg) && ss >> ret && ss.eof()))
            throw std::bad_cast();
        return ret;
    }
};

template <typename Target, typename Source>
class lexical_cast_t<Target, Source, true> {
   public:
    static Target cast(Source&& arg) { return std::forward<Source>(arg); }
};

template <typename Target>
class lexical_cast_t<Target, string_t, false> {
   public:
    static Target cast(string_t&& arg) {
        Target ret;
        std::istringstream ss(arg);
        if (!(ss >> ret && ss.eof())) throw std::bad_cast();
        return ret;
    }
};

template <typename Source>
class lexical_cast_t<string_t, Source, false> {
   public:
    static string_t cast(Source&& arg) {
        std::ostringstream ss;
        if (!(ss << std::forward<Source>(arg))) throw std::bad_cast();
        return ss.str();
    }
};

template <typename Target, typename Source>
constexpr bool is_same_v = std::is_same<Target, Source>::value;

template <typename Target, typename Source>
Target lexical_cast(Source&& arg) {
    return lexical_cast_t<Target, Source, is_same_v<Target, Source>>::cast(
        std::forward<Source>(arg));
};

template <typename T>
struct is_string
    : std::integral_constant<
          bool, is_same_v<string_t, typename std::remove_cv<T>::type>> {};

template <typename T>
constexpr bool is_string_v = is_string<T>::value;

template <typename T>
string_t as_string(T&& arg) {
    string_t str{lexical_cast<string_t>(std::forward<T>(arg))};
    if (is_string_v<std::remove_reference_t<T>>) {
        return "\"" + str + "\"";
    } else {
        return str;
    }
}

template <typename T>
constexpr bool is_integral_v = std::is_integral<T>::value;

template <typename T>
constexpr bool is_floating_point_v = std::is_floating_point<T>::value;

template <typename T>
struct is_legal_type
    : std::integral_constant<bool, is_string_v<T> || is_integral_v<T> ||
                                       is_floating_point_v<T>> {};

template <typename T>
constexpr bool is_legal_type_v = is_legal_type<T>::value;

enum class type_category { IllegalType = 0, Integral, FloatingPoint, String };
template <typename T, bool is_integral, bool is_floating_point, bool is_string>
struct type_name_impl {
    static constexpr enum type_category value { type_category::IllegalType };
};

template <typename T>
struct type_name_impl<T, true, false, false> {
    static constexpr enum type_category value { type_category::Integral };
};

template <typename T>
struct type_name_impl<T, false, true, false> {
    static constexpr enum type_category value { type_category::FloatingPoint };
};

template <typename T>
struct type_name_impl<T, false, false, true> {
    static constexpr enum type_category value { type_category::String };
};

template <typename T>
string_t type_name() {
    static std::unordered_map<enum type_category, string_t> names {
        std::make_pair(type_category::IllegalType, "IllegalType"),
        std::make_pair(type_category::Integral, "Integral"),
        std::make_pair(type_category::FloatingPoint, "FloatingPoint"),
        std::make_pair(type_category::String, "String")
    };
    auto category = type_name_impl<T, is_integral_v<T>, is_floating_point_v<T>,
                                   is_string_v<T>>::value;
    return names[category];
};

}  // namespace detail

class program_options_error : public std::exception {
   public:
    template <typename T>
    program_options_error(T&& msg) : msg_(std::forward<T>(msg)) {}
    const char* what() const noexcept { return msg_.c_str(); }

   private:
    string_t msg_;
};

template <typename T>
class default_reader {
   public:
    T operator()(const string_t& str) const {
        return detail::lexical_cast<T>(str);
    }
};

template <typename T>
class range_reader {
   public:
    range_reader(T&& begin, T&& end)
        : begin_(std::forward<T>(begin)), end_(std::forward<T>(end)) {}
    T operator()(const string_t& str) const {
        T ret{default_reader<T>()(str)};
        if (ret > end_ || ret < begin_)
            throw program_options_error("range error");
        return ret;
    }

   private:
    T begin_, end_;
};

template <typename T>
range_reader<T> range(T&& begin, T&& end) {
    return range_reader<T>(std::forward<T>(begin), std::forward<T>(end));
}

template <typename T>
class oneof_reader {
   public:
    T operator()(const string_t& str) {
        T ret{default_reader<T>()(str)};
        if (allowed_.find(ret) == allowed_.end())
            throw program_options_error("oneof error");
        return ret;
    }

    void add(T&& val) { allowed_.insert(std::forward<T>(val)); }
    template <typename... U>
    void add(T&& val, U&&... vals) {
        allowed_.insert(std::forward<T>(val));
        add(std::forward<U>(vals)...);
    }

   private:
    std::unordered_set<T> allowed_;
};

template <typename T, typename... U>
oneof_reader<T> oneof(U&&... vals) {
    oneof_reader<T> ret;
    ret.add(std::forward<U>(vals)...);
    return ret;
}

class parser {
   public:
    parser& add(const string_t& name, char short_name = 0,
                const string_t& description = "") {
        if (options_.count(name))
            throw program_options_error("multiple definition: " + name);
        options_.insert(
            std::make_pair(name, option_ptr(new option_without_value(
                                     name, short_name, description))));
        ordered_.push_back(options_.at(name));
        return *this;
    }

    template <typename T>
    parser& add(const string_t& name, char short_name = 0,
                const string_t& description = "", bool is_required = true,
                T&& default_value = T()) {
        if (!detail::is_legal_type_v<T>)
            throw program_options_error("illegal type: " + name);
        return add(name, short_name, description, is_required,
                   std::forward<T>(default_value), default_reader<T>());
    }

    template <typename T, typename U>
    parser& add(const string_t& name, char short_name = 0,
                const string_t& description = "", bool is_required = true,
                T&& default_value = T(), U&& reader = U()) {
        if (!detail::is_legal_type_v<T>)
            throw program_options_error("illegal type: " + name);
        if (options_.count(name))
            throw program_options_error("multiple definition: " + name);
        options_.insert(std::make_pair(
            name,
            option_ptr(new option_with_value_with_reader<T, U>(
                name, short_name, is_required, std::forward<T>(default_value),
                description, std::forward<U>(reader)))));
        ordered_.push_back(options_.at(name));
        return *this;
    };

    void set_footer(const string_t& footer) { footer_ = footer; }

    void set_program_name(const string_t& program_name) {
        program_name_ = program_name;
    }

    bool exist(const string_t& name) const {
        auto iter = options_.find(name);
        if (iter == options_.end())
            throw program_options_error("there is no flag: --" + name);
        return iter->second->has_set();
    }

    template <typename T>
    const T& get(const string_t& name) const {
        if (options_.count(name) == 0)
            throw program_options_error("there is no flag: --" + name);
        auto p = dynamic_cast<const option_with_value<T>*>(
            options_.find(name)->second.get());
        if (p == nullptr)
            throw program_options_error("type mismatch flag '" + name + "'");
        return p->get();
    }

    const std::vector<string_t>& rest() const { return others_; }

    bool parse(const string_t& arg) {
        std::vector<string_t> args;
        string_t buf;
        bool in_quote{false};
        for (string_t::size_type i = 0; i != arg.length(); ++i) {
            if (arg[i] == '\"') {
                in_quote = !in_quote;
                continue;
            }
            if (arg[i] == ' ' && !in_quote) {
                args.push_back(buf);
                buf.clear();
                continue;
            }
            if (arg[i] == '\\') {
                ++i;
                if (i >= arg.length()) {
                    errors_.emplace_back(
                        "unexpected occurrence of '\\' at end of string");
                    return false;
                }
            }
            buf += arg[i];
        }

        if (in_quote) {
            errors_.emplace_back("quote is not closed");
            return false;
        }

        if (!buf.empty()) {
            args.push_back(buf);
        }

        return parse(args);
    }

    bool parse(const std::vector<string_t>& args) {
        auto argc = args.size();
        std::vector<const char*> argv(argc);
        std::transform(args.begin(), args.end(), argv.begin(),
                       [](const string_t& str) { return str.c_str(); });
        return parse(argc, argv.data());
    }

    bool parse(int argc, const char* const argv[]) {
        errors_.clear();
        others_.clear();

        if (argc < 1) {
            errors_.emplace_back("argument number must be longer than 0");
            return false;
        }
        if (program_name_.empty()) {
            program_name_ = argv[0];
        }

        std::unordered_map<char, string_t> lookup;
        for (auto& item : options_) {
            if (item.first.empty()) continue;
            char short_name{item.second->short_name()};
            if (short_name) {
                if (lookup.count(short_name)) {
                    lookup[short_name].clear();
                    errors_.push_back(string_t("short option '") + short_name +
                                      "' is ambiguous");
                    return false;
                }
                lookup[short_name] = item.first;
            }
        }

        for (int i = 1; i != argc; ++i) {
            if (strncmp(argv[i], "--", 2) == 0) {
                const char* p{strchr(argv[i] + 2, '=')};
                if (p) {
                    string_t name{argv[i] + 2, p};
                    string_t value{p + 1};
                    set_option(name, value);
                } else {
                    string_t name{argv[i] + 2};
                    if (options_.count(name) == 0) {
                        errors_.push_back("undefined option: --" + name);
                        continue;
                    }
                    if (options_[name]->has_value()) {
                        if (i + 1 >= argc) {
                            errors_.push_back("option needs value: --" + name);
                            continue;
                        } else {
                            set_option(name, argv[i++]);
                        }
                    } else {
                        set_option(name);
                    }
                }
            } else if (strncmp(argv[i], "-", 1) == 0) {
                if (!argv[i][1]) continue;
                char last{argv[i][1]};
                for (int j = 2; argv[i][j]; ++j) {
                    if (lookup.count(last) == 0) {
                        errors_.push_back(
                            string_t("undefined short option: -") + last);
                    } else if (lookup[last].empty()) {
                        errors_.push_back(
                            string_t("ambiguous short options: -") + last);
                    } else {
                        set_option(lookup[last]);
                    }
                    last = argv[i][j];
                }

                if (lookup.count(last) == 0) {
                    errors_.push_back(string_t("undefined short option: -") +
                                      last);
                    continue;
                } else if (lookup[last].empty()) {
                    errors_.push_back(string_t("ambiguous short options: -") +
                                      last);
                    continue;
                }

                if (i + 1 < argc && options_[lookup[last]]->has_value()) {
                    set_option(lookup[last], argv[++i]);
                } else {
                    set_option(lookup[last]);
                }
            } else {
                others_.push_back(argv[i]);
            }
        }

        for (auto& item : options_) {
            if (!item.second->is_valid()) {
                errors_.push_back("need option: --" + item.first);
            }
        }

        return errors_.empty();
    }

    void parse_check(const string_t& arg) {
        if (!options_.count("help")) add("help", '?', "print this message");
        check(0, parse(arg));
    }

    void parse_check(const std::vector<string_t>& args) {
        if (!options_.count("help")) add("help", '?', "print this message");
        check(args.size(), parse(args));
    }

    void parse_check(int argc, const char* const argv[]) {
        if (!options_.count("help")) add("help", '?', "print this message");
        check(argc, parse(argc, argv));
    }

    string_t error() const { return errors_.empty() ? "" : errors_[0]; }

    string_t all_errors() const {
        std::ostringstream oss;
        for (auto& error : errors_) {
            oss << error << std::endl;
        }
        return oss.str();
    }

    string_t usage() const {
        std::ostringstream oss;
        oss << "Usage: " << program_name_ << " ";
        for (auto& item : ordered_) {
            if (item->is_required()) oss << item->short_description() << " ";
        }

        oss << "[options] ... " << footer_ << std::endl;
        oss << "Options:" << std::endl;

        using length_t = decltype(ordered_)::size_type;
        length_t max_width{(*std::max_element(ordered_.begin(), ordered_.end(),
                                              [](const option_ptr& lhs,
                                                 const option_ptr& rhs) {
                                                  return lhs->name().length() <
                                                         rhs->name().length();
                                              }))
                               ->name()
                               .length()};
        for (auto& item : ordered_) {
            if (item->short_name()) {
                oss << "  -" << item->short_name() << ", ";
            } else {
                oss << "      ";
            }

            oss << "--" << item->name();
            for (length_t j = item->name().length(); j != max_width + 4; ++j)
                oss << ' ';
            oss << item->description() << std::endl;
        }
        return oss.str();
    }

   private:
    void check(int argc, bool ok) {
        if ((argc == 1 && !ok) || exist("help")) {
            std::cerr << usage();
            exit(0);
        }
        if (!ok) {
            std::cerr << error() << std::endl << usage();
            exit(-1);
        }
    }

    void set_option(const string_t& name) {
        if (options_.count(name) == 0) {
            errors_.push_back("undefined options: --" + name);
            return;
        }
        if (!options_[name]->set()) {
            errors_.push_back("option needs value: --" + name);
            return;
        }
    }

    void set_option(const string_t& name, const string_t& value) {
        if (options_.count(name) == 0) {
            errors_.push_back("undefined options: --" + name);
            return;
        }
        if (!options_[name]->set(value)) {
            errors_.push_back("option value is invalid: --" + name + "=" +
                              value);
            return;
        }
    }

    class option_base {
       public:
        virtual ~option_base() = default;

        virtual bool has_value() const = 0;
        virtual bool set() = 0;
        virtual bool set(const string_t& value) = 0;
        virtual bool has_set() const = 0;
        virtual bool is_valid() const = 0;
        virtual bool is_required() const = 0;

        virtual const string_t& name() const = 0;
        virtual char short_name() const = 0;
        virtual const string_t& description() const = 0;
        virtual string_t short_description() const = 0;
    };

    class option_without_value : public option_base {
       public:
        option_without_value(const string_t& name, char short_name,
                             const string_t& description)
            : name_(name),
              short_name_(short_name),
              description_(description),
              has_set_(false) {}

        virtual bool has_value() const override { return false; }
        virtual bool set() override { return (has_set_ = true); }
        virtual bool set(const string_t&) override { return false; }
        virtual bool has_set() const override { return has_set_; }
        virtual bool is_valid() const override { return true; }
        virtual bool is_required() const override { return false; }
        virtual const string_t& name() const override { return name_; }
        virtual char short_name() const override { return short_name_; }
        virtual const string_t& description() const override {
            return description_;
        }
        virtual string_t short_description() const override {
            return "--" + name_;
        }

       private:
        string_t name_;
        char short_name_;
        string_t description_;
        bool has_set_;
    };

    template <typename T>
    class option_with_value : public option_base {
       public:
        option_with_value(const string_t& name, char short_name,
                          bool is_required, const T& default_value,
                          const string_t& description)
            : name_(name),
              short_name_(short_name),
              is_required_(is_required),
              has_set_(false),
              default_value_(default_value),
              actual_value_(default_value),
              description_(full_description(description)) {}

        const T& get() const { return actual_value_; }
        virtual bool has_value() const override { return true; }
        virtual bool set() override { return false; }
        virtual bool set(const string_t& value) override {
            try {
                actual_value_ = read(value);
            } catch (const std::exception& e) {
                return false;
            }
            return has_set_ = true;
        }
        virtual bool has_set() const override { return has_set_; }
        virtual bool is_valid() const override {
            return (!is_required_) || has_set_;
        }
        virtual bool is_required() const override { return is_required_; }
        virtual const string_t& name() const override { return name_; }
        virtual char short_name() const override { return short_name_; }
        virtual const string_t& description() const override {
            return description_;
        }
        virtual string_t short_description() const override {
            return "--" + name_ + "=" + detail::type_name<T>();
        }

       protected:
        string_t full_description(const string_t& description) {
            return description + " (" + detail::type_name<T>() +
                   (is_required_
                        ? ""
                        : (" [=" + detail::as_string(default_value_) + "]")) +
                   ")";
        }

        virtual T read(const string_t& str) = 0;
        string_t name_;
        char short_name_;
        bool is_required_;
        bool has_set_;
        T default_value_;
        T actual_value_;
        string_t description_;
    };

    template <typename T, typename U>
    class option_with_value_with_reader : public option_with_value<T> {
       public:
        option_with_value_with_reader(const string_t& name, char short_name,
                                      bool is_required, const T& default_value,
                                      const string_t& description, U&& reader)
            : option_with_value<T>(name, short_name, is_required, default_value,
                                   description),
              reader_(std::forward<U>(reader)) {}

       private:
        T read(const string_t& str) { return reader_(str); }

        U reader_;
    };

    using option_ptr = std::shared_ptr<option_base>;
    std::unordered_map<string_t, option_ptr> options_;
    std::vector<option_ptr> ordered_;
    string_t footer_;
    string_t program_name_;
    std::vector<string_t> others_;
    std::vector<string_t> errors_;
};
}  // namespace program_options
