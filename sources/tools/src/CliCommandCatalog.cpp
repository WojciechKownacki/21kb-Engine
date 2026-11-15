#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The machine-readable command catalog (`kb_cli commands`). Dispatch (the command table in
// CliDispatch.cpp) names the commands; the usage text `kb_cli help` prints is the one place that
// describes them, and this file reads it: a command's purpose is its row, its subcommands and options
// (name, type, required, default) come from the syntax lines under the row. Nothing is described twice.
namespace kb::cli {
namespace {

using kb::core::JsonValue;

constexpr std::size_t kSyntaxIndent = 17U;

struct OptionSpec {
    std::string name;
    std::string type; // flag | number | path | string | enum
    bool positional = false;
    bool repeatable = false;
    bool topLevel = false; // outside every bracket and parenthesis of its form
    bool bracketed = false; // inside [ ]: optional, so a literal value is its default
    std::string defaultValue;
    std::vector<std::string> choices;
};

struct FormSpec {
    std::vector<std::string> names; // the subcommands this syntax line describes; empty: the command itself
    std::vector<OptionSpec> options;
    bool alternatives = false; // a top-level '|' makes every option of the form optional
};

struct UsageRow {
    std::string name;
    std::string text; // the row and its lines, as `kb_cli help <command>` prints them
    std::string purpose;
    std::vector<FormSpec> forms;
    FormSpec notes; // options of note lines and of lines that every subcommand shares
};

[[nodiscard]] std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1U);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1U);
    return text;
}

[[nodiscard]] bool IsNameChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-'; }

[[nodiscard]] bool IsNumeric(std::string_view text) {
    if (text.empty()) return false;
    return std::ranges::all_of(text, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-'; });
}

// The end of a value token: stops at white space, or at a ']' or ')' that closes an outer group.
// <...> and "..." may hold spaces.
[[nodiscard]] std::size_t ValueEnd(std::string_view line, std::size_t at) {
    int balance = 0;
    while (at < line.size()) {
        const char c = line[at];
        if (c == '<' || c == '"') {
            const std::size_t close = line.find(c == '<' ? '>' : '"', at + 1U);
            if (close == std::string_view::npos) return line.size();
            at = close + 1U;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) break;
        if (c == '[' || c == '(') ++balance;
        else if (c == ']' || c == ')') {
            if (balance == 0) break;
            --balance;
        }
        ++at;
    }
    return at;
}

[[nodiscard]] bool StartsValue(std::string_view line, std::size_t at) {
    if (at >= line.size()) return false;
    const char c = line[at];
    if (c == '.' && line.substr(at, 3U) == "...") return false;
    return c == '<' || c == '"' || c == '\'' || c == '/' || c == '#' || std::isalnum(static_cast<unsigned char>(c)) != 0;
}

[[nodiscard]] std::string PlaceholderType(std::string_view inner) {
    std::string lower;
    for (const char c : inner) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool extension = lower.find('.') != std::string::npos && !IsNumeric(lower) && lower.find(' ') == std::string::npos;
    if (extension) return "path";
    if (lower.find('|') != std::string::npos) return "string";
    constexpr std::string_view numbers[]{ "n", "number", "seconds", "percent", "metres", "count", "milliseconds", "bytes", "index", "frames", "level", "pixels", "x", "z" };
    if (IsNumeric(lower) || std::ranges::find(numbers, lower) != std::end(numbers)) return "number";
    constexpr std::string_view pathWords[]{ "dir", "path", "file", "folder", "source", "executable", "recording", "directory" };
    if (std::ranges::any_of(pathWords, [&](std::string_view word) { return lower.find(word) != std::string::npos; }))
        return "path";
    return "string";
}

void ApplyValue(OptionSpec& option, std::string_view value, bool allowDefault) {
    if (value.size() > 2U && value.front() == '<' && value.back() == '>' && value.find('<', 1U) == std::string_view::npos) {
        option.type = PlaceholderType(value.substr(1U, value.size() - 2U));
    } else if (value.find('<') != std::string_view::npos) {
        option.type = "string";
    } else if (value.find('=') != std::string_view::npos) {
        option.type = "string"; // name=value
    } else if (value.find('|') != std::string_view::npos) {
        option.type = "enum";
        std::size_t start = 0U;
        while (start <= value.size()) {
            std::size_t end = value.find('|', start);
            if (end == std::string_view::npos) end = value.size();
            if (end > start) option.choices.emplace_back(value.substr(start, end - start));
            start = end + 1U;
        }
    } else if (IsNumeric(value)) {
        option.type = "number";
        if (allowDefault && option.bracketed) option.defaultValue = std::string{ value };
    } else if (value.find('/') != std::string_view::npos && value.find(',') == std::string_view::npos && value.find('<') == std::string_view::npos) {
        option.type = "path";
        if (allowDefault && option.bracketed) option.defaultValue = std::string{ value };
    } else {
        option.type = "string";
    }
}

// Reads the syntax of usage lines: --option <value>, [optional groups], (alternatives), <positional>
// arguments. A parenthesis that starts with ordinary words is a note and is skipped.
class SyntaxScanner {
public:
    [[nodiscard]] bool InGroup() const noexcept { return !groups_.empty(); }
    void Reset() noexcept { groups_.clear(); noteDepth_ = 0; }

    // `note`: the line comments on the syntax - its options are optional and a literal value is an example.
    void Feed(std::string_view line, FormSpec& form, bool note) {
        std::size_t i = 0U;
        while (i < line.size()) {
            const char c = line[i];
            if (!groups_.empty() && groups_.back() == 'n') {
                if (c == '(') ++noteDepth_;
                if (c == ')' && --noteDepth_ == 0) groups_.pop_back();
                ++i;
                continue;
            }
            if (c == '[') {
                groups_.push_back('[');
            } else if (c == ']') {
                const auto open = std::ranges::find(groups_.rbegin(), groups_.rend(), '[');
                if (open != groups_.rend()) groups_.erase(open.base() - 1, groups_.end());
            } else if (c == '(') {
                if (LooksLikeSyntaxGroup(line.substr(i + 1U))) {
                    groups_.push_back('(');
                } else {
                    groups_.push_back('n');
                    noteDepth_ = 1;
                }
            } else if (c == ')') {
                if (!groups_.empty() && groups_.back() == '(') groups_.pop_back();
            } else if (c == '|') {
                if (groups_.empty() && (i == 0U || line[i - 1U] == ' ')) form.alternatives = true;
            } else if (c == '-' && IsOptionStart(line, i)) {
                i = ScanOption(line, i, form, note);
                continue;
            } else if (c == '<') {
                const std::size_t close = line.find('>', i + 1U);
                if (close == std::string_view::npos) break;
                const std::string_view inner = line.substr(i + 1U, close - i - 1U);
                if (i > 0U && std::string_view{ " [(|" }.find(line[i - 1U]) == std::string_view::npos) {
                    i = close + 1U; // part of a longer token, such as [@<x>:<z>]
                    continue;
                }
                OptionSpec argument;
                argument.name = std::string{ inner };
                argument.positional = true;
                argument.type = PlaceholderType(inner);
                argument.topLevel = groups_.empty() && !note;
                argument.bracketed = !groups_.empty();
                std::size_t after = close + 1U;
                while (after < line.size() && line[after] == ' ') ++after;
                if (line.substr(after, 3U) == "...") argument.repeatable = true;
                AddOption(form, std::move(argument));
                i = close;
            }
            ++i;
        }
    }

private:
    [[nodiscard]] static bool IsOptionStart(std::string_view line, std::size_t at) {
        return at + 2U < line.size() && line[at + 1U] == '-' && std::isalpha(static_cast<unsigned char>(line[at + 2U])) != 0 &&
            (at == 0U || std::string_view{ " [(|" }.find(line[at - 1U]) != std::string_view::npos);
    }

    [[nodiscard]] static std::size_t NameEnd(std::string_view line, std::size_t at) {
        std::size_t end = at + 2U;
        while (end < line.size() && IsNameChar(line[end])) ++end;
        return end;
    }

    // A parenthesis is syntax when it starts with an option whose value is followed by more syntax,
    // not by words: (--name <Entity> ... | --terrain <file>) against (--create makes what ...).
    [[nodiscard]] static bool LooksLikeSyntaxGroup(std::string_view rest) {
        if (rest.starts_with('<')) return true;
        if (!rest.starts_with("--")) return false;
        std::size_t at = NameEnd(rest, 0U);
        if (at < rest.size() && std::string_view{ ",:;." }.find(rest[at]) != std::string_view::npos) return false;
        while (at < rest.size() && rest[at] == ' ') ++at;
        if (StartsValue(rest, at)) {
            at = ValueEnd(rest, at);
            while (at < rest.size() && rest[at] == ' ') ++at;
        }
        return at >= rest.size() || std::string_view{ ")]|[-.<(" }.find(rest[at]) != std::string_view::npos;
    }

    std::size_t ScanOption(std::string_view line, std::size_t at, FormSpec& form, bool note) {
        const std::size_t end = NameEnd(line, at);
        OptionSpec option;
        option.name = std::string{ line.substr(at, end - at) };
        option.topLevel = groups_.empty() && !note;
        option.bracketed = std::ranges::find(groups_, '[') != groups_.end();
        option.type = "flag";
        std::size_t next = end;
        while (next < line.size() && line[next] == ' ') ++next;
        if (next > end && StartsValue(line, next)) {
            const std::size_t valueEnd = ValueEnd(line, next);
            ApplyValue(option, line.substr(next, valueEnd - next), !note);
            next = valueEnd;
            while (next < line.size() && line[next] == ' ') ++next;
            if (line.substr(next, 3U) == "..." || line.substr(next, 5U) == "[...]") option.repeatable = true;
        }
        AddOption(form, std::move(option));
        return next;
    }

    static void AddOption(FormSpec& form, OptionSpec option) {
        for (OptionSpec& known : form.options) {
            if (known.name != option.name || known.positional != option.positional) continue;
            // A later mention only adds what the first lacked: a value for a flag, the repeat mark.
            if (known.type == "flag") {
                known.type = option.type;
                known.choices = option.choices;
                if (known.defaultValue.empty()) known.defaultValue = option.defaultValue;
            }
            known.repeatable = known.repeatable || option.repeatable;
            known.topLevel = known.topLevel && option.topLevel;
            return;
        }
        form.options.push_back(std::move(option));
    }

    std::vector<char> groups_;
    int noteDepth_ = 0;
};

// `show|check`, `param add|remove`, `kinds`: the subcommand words that start a syntax line.
[[nodiscard]] std::optional<std::pair<std::vector<std::string>, std::size_t>> SubcommandHead(std::string_view text) {
    std::vector<std::vector<std::string>> words;
    std::size_t at = 0U;
    const auto isWord = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0 || std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '-'; };
    while (at < text.size() && words.size() < 3U) {
        std::vector<std::string> alternatives;
        std::size_t end = at;
        for (;;) {
            std::size_t wordEnd = end;
            if (wordEnd >= text.size() || std::islower(static_cast<unsigned char>(text[wordEnd])) == 0) return std::nullopt;
            while (wordEnd < text.size() && isWord(text[wordEnd])) ++wordEnd;
            alternatives.emplace_back(text.substr(end, wordEnd - end));
            end = wordEnd;
            if (end < text.size() && (text[end] == '|' || text[end] == '/')) { ++end; continue; }
            break;
        }
        if (end < text.size() && text[end] != ' ') return std::nullopt;
        words.push_back(std::move(alternatives));
        at = end;
        while (at < text.size() && text[at] == ' ') ++at;
        if (at >= text.size() || std::string_view{ "-[(<|" }.find(text[at]) != std::string_view::npos) break;
    }
    if (words.empty() || (at < text.size() && std::string_view{ "-[(<|" }.find(text[at]) == std::string_view::npos)) return std::nullopt;
    std::vector<std::string> names{ std::string{} };
    for (const auto& alternatives : words) {
        std::vector<std::string> next;
        for (const std::string& prefix : names)
            for (const std::string& word : alternatives) next.push_back(prefix.empty() ? word : prefix + ' ' + word);
        names = std::move(next);
    }
    return std::make_pair(std::move(names), at);
}

[[nodiscard]] std::string FirstSentence(std::string_view text) {
    text = Trim(text);
    for (std::size_t at = text.find(". "); at != std::string_view::npos; at = text.find(". ", at + 1U)) {
        if (at >= 3U && (text.substr(at - 3U, 3U) == "e.g" || text.substr(at - 3U, 3U) == "i.e")) continue;
        return std::string{ text.substr(0U, at + 1U) };
    }
    return std::string{ text };
}

void ParseRow(UsageRow& row, const std::vector<std::string>& body) {
    const std::string_view firstLine = std::string_view{ row.text }.substr(0U, row.text.find('\n'));
    const std::size_t nameEnd = firstLine.find(' ', 2U);
    std::string purpose{ Trim(nameEnd == std::string_view::npos ? std::string_view{} : firstLine.substr(nameEnd)) };
    SyntaxScanner scanner;
    FormSpec* open = nullptr;
    bool sawSyntax = false;
    bool sawSubcommand = false;
    for (const std::string& line : body) {
        const std::string_view text = Trim(line);
        if (text.empty()) continue;
        const std::size_t indent = line.find_first_not_of(' ');
        if (scanner.InGroup()) {
            scanner.Feed(text, open != nullptr ? *open : row.notes, open == nullptr);
            continue;
        }
        if (indent < kSyntaxIndent) {
            if (!sawSyntax) {
                purpose += ' ';
                purpose += text;
            } else {
                scanner.Feed(text, row.notes, true);
            }
            continue;
        }
        const auto head = indent == kSyntaxIndent ? SubcommandHead(text) : std::nullopt;
        if (head) {
            row.forms.push_back(FormSpec{ .names = head->first, .options = {}, .alternatives = false });
            open = &row.forms.back();
            sawSyntax = sawSubcommand = true;
            scanner.Reset();
            scanner.Feed(text.substr(head->second), *open, false);
        } else if (indent == kSyntaxIndent && sawSubcommand) {
            scanner.Feed(text, row.notes, true);
        } else if (indent == kSyntaxIndent && text.starts_with("--") && open != nullptr && !sawSubcommand) {
            row.forms.push_back(FormSpec{});
            open = &row.forms.back();
            scanner.Reset();
            scanner.Feed(text, *open, false);
        } else {
            if (open == nullptr) {
                row.forms.push_back(FormSpec{});
                open = &row.forms.back();
                sawSyntax = true;
            }
            scanner.Feed(text, *open, false);
        }
    }
    row.purpose = FirstSentence(purpose);
}

[[nodiscard]] const std::vector<UsageRow>& UsageRows() {
    static const std::vector<UsageRow> rows = [] {
        std::vector<UsageRow> result;
        std::vector<std::vector<std::string>> bodies;
        const std::string usage = CliUsageText();
        std::size_t start = 0U;
        bool inCommands = false;
        while (start < usage.size()) {
            std::size_t end = usage.find('\n', start);
            if (end == std::string::npos) end = usage.size();
            const std::string line = usage.substr(start, end - start);
            start = end + 1U;
            if (line == "Commands:") { inCommands = true; continue; }
            if (!inCommands || Trim(line).empty()) continue;
            if (line.front() != ' ') break;
            if (line.size() > 2U && line[0] == ' ' && line[1] == ' ' && line[2] != ' ') {
                UsageRow row;
                row.name = line.substr(2U, line.find(' ', 2U) - 2U);
                row.text = line + '\n';
                result.push_back(std::move(row));
                bodies.emplace_back();
            } else if (!result.empty()) {
                result.back().text += line + '\n';
                bodies.back().push_back(line);
            }
        }
        for (std::size_t index = 0U; index < result.size(); ++index) ParseRow(result[index], bodies[index]);
        return result;
    }();
    return rows;
}

[[nodiscard]] const UsageRow* FindRow(std::string_view name) {
    const auto& rows = UsageRows();
    const auto found = std::ranges::find(rows, name, &UsageRow::name);
    return found == rows.end() ? nullptr : &*found;
}

// The options of one syntax line merged into the command's: required only where every line requires it.
void MergeInto(std::vector<OptionSpec>& target, const FormSpec& form, bool first) {
    for (const OptionSpec& option : form.options) {
        const bool required = option.topLevel && !form.alternatives;
        const auto known = std::ranges::find_if(target, [&](const OptionSpec& other) { return other.name == option.name && other.positional == option.positional; });
        if (known == target.end()) {
            OptionSpec added = option;
            added.topLevel = required && first;
            target.push_back(std::move(added));
        } else {
            known->topLevel = known->topLevel && required;
            if (known->type == "flag") {
                known->type = option.type;
                known->choices = option.choices;
            }
            if (known->defaultValue.empty()) known->defaultValue = option.defaultValue;
            known->repeatable = known->repeatable || option.repeatable;
        }
    }
    if (first) return;
    for (OptionSpec& known : target) {
        if (std::ranges::none_of(form.options, [&](const OptionSpec& option) { return option.name == known.name && option.positional == known.positional; }))
            known.topLevel = false;
    }
}

[[nodiscard]] JsonValue OptionJson(const OptionSpec& option, bool required) {
    auto row = JsonValue::MakeObject();
    row.Set("name", JsonValue::MakeString(option.name));
    row.Set("type", JsonValue::MakeString(option.type));
    row.Set("required", JsonValue::MakeBool(required));
    row.Set("default", option.defaultValue.empty() ? JsonValue::MakeNull() : JsonValue::MakeString(option.defaultValue));
    if (!option.choices.empty()) {
        auto choices = JsonValue::MakeArray();
        for (const std::string& choice : option.choices) choices.Append(JsonValue::MakeString(choice));
        row.Set("choices", std::move(choices));
    }
    if (option.repeatable) row.Set("repeatable", JsonValue::MakeBool(true));
    if (option.positional) row.Set("positional", JsonValue::MakeBool(true));
    return row;
}

[[nodiscard]] JsonValue OptionsJson(const std::vector<OptionSpec>& options) {
    auto array = JsonValue::MakeArray();
    for (const OptionSpec& option : options) array.Append(OptionJson(option, option.topLevel));
    return array;
}

[[nodiscard]] std::string_view JsonKindName(CliJsonOutput kind) noexcept {
    switch (kind) {
    case CliJsonOutput::Envelope: return "envelope";
    case CliJsonOutput::Native: return "native";
    case CliJsonOutput::Partial: return "partial";
    case CliJsonOutput::Protocol: return "protocol";
    }
    return "envelope";
}

[[nodiscard]] JsonValue CommandJson(const CliCommandDescriptor& command) {
    auto row = JsonValue::MakeObject();
    row.Set("name", JsonValue::MakeString(std::string{ command.name }));
    row.Set("available", JsonValue::MakeBool(command.available));
    row.Set("mcpTool", command.mcpTool.empty() ? JsonValue::MakeNull() : JsonValue::MakeString(std::string{ command.mcpTool }));
    row.Set("json", JsonValue::MakeString(std::string{ JsonKindName(command.json) }));
    const UsageRow* usage = FindRow(command.name);
    row.Set("purpose", JsonValue::MakeString(usage != nullptr ? usage->purpose : std::string{}));

    std::vector<OptionSpec> options;
    auto subcommands = JsonValue::MakeArray();
    std::vector<std::string> names;
    std::vector<std::vector<OptionSpec>> named;
    if (usage != nullptr) {
        bool first = true;
        for (const FormSpec& form : usage->forms) {
            if (form.names.empty()) {
                MergeInto(options, form, first);
                first = false;
                continue;
            }
            std::vector<OptionSpec> own;
            MergeInto(own, form, true);
            for (const std::string& name : form.names) {
                const auto known = std::ranges::find(names, name);
                if (known == names.end()) {
                    names.push_back(name);
                    named.push_back(own);
                    continue;
                }
                // The same subcommand on another line: more options for it.
                std::vector<OptionSpec>& target = named[static_cast<std::size_t>(known - names.begin())];
                for (OptionSpec extra : own) {
                    if (std::ranges::none_of(target, [&](const OptionSpec& option) { return option.name == extra.name && option.positional == extra.positional; })) target.push_back(std::move(extra));
                }
            }
        }
        for (std::size_t index = 0U; index < names.size(); ++index) {
            auto entry = JsonValue::MakeObject();
            entry.Set("name", JsonValue::MakeString(names[index]));
            entry.Set("options", OptionsJson(named[index]));
            subcommands.Append(std::move(entry));
        }
        // Options of note lines, and of lines that every subcommand shares.
        for (OptionSpec extra : usage->notes.options) {
            const bool known = std::ranges::any_of(usage->forms, [&](const FormSpec& form) {
                return std::ranges::any_of(form.options, [&](const OptionSpec& option) { return option.name == extra.name; });
            });
            if (known) continue;
            extra.topLevel = false;
            options.push_back(std::move(extra));
        }
    }
    row.Set("options", OptionsJson(options));
    row.Set("subcommands", std::move(subcommands));
    return row;
}

} // namespace

std::vector<std::string> CliUsageCommandNames() {
    std::vector<std::string> names;
    for (const UsageRow& row : UsageRows()) names.push_back(row.name);
    return names;
}

std::string CliCommandUsageBlock(std::string_view command) {
    const UsageRow* row = FindRow(command);
    return row == nullptr ? std::string{} : row->text;
}

std::string CliCatalogJson() {
    auto result = JsonValue::MakeObject();
    result.Set("schema", JsonValue::MakeString("21kb.cli-commands/v1"));

    struct GlobalOption { std::string_view name; std::string_view type; std::string_view purpose; };
    constexpr GlobalOption globalOptions[]{
        { "--json", "flag", "Print one result object instead of text (put it before the command)." },
        { "--dry-run", "flag", "Only for the commands that name it." },
        { "--command-timeout-ms", "number", "Bound the entire command, 1..86400000 milliseconds (Windows)." },
        { "--command-cancel-file", "path", "Stop when this caller-owned file exists." } };
    auto global = JsonValue::MakeArray();
    for (const GlobalOption& option : globalOptions) {
        auto row = JsonValue::MakeObject();
        row.Set("name", JsonValue::MakeString(std::string{ option.name }));
        row.Set("type", JsonValue::MakeString(std::string{ option.type }));
        row.Set("required", JsonValue::MakeBool(false));
        row.Set("default", JsonValue::MakeNull());
        row.Set("purpose", JsonValue::MakeString(std::string{ option.purpose }));
        global.Append(std::move(row));
    }
    result.Set("globalOptions", std::move(global));

    struct ExitCode { int code; std::string_view meaning; };
    constexpr ExitCode exitCodeList[]{ { 0, "success" }, { 1, "failure: bad arguments, unknown command, failed check or build" },
        { 124, "timeout" }, { 125, "supervisor error" }, { 130, "cancelled" } };
    auto exitCodes = JsonValue::MakeArray();
    for (const ExitCode& exit : exitCodeList) {
        auto row = JsonValue::MakeObject();
        row.Set("code", JsonValue::MakeNumber(exit.code));
        row.Set("meaning", JsonValue::MakeString(std::string{ exit.meaning }));
        exitCodes.Append(std::move(row));
    }
    result.Set("exitCodes", std::move(exitCodes));

    auto kinds = JsonValue::MakeObject();
    kinds.Set("envelope", JsonValue::MakeString("--json wraps the text output in a 21kb.cli-result/v1 object with ok, exitCode and errors."));
    kinds.Set("native", JsonValue::MakeString("The command prints its own versioned JSON object; --json keeps it unchanged."));
    kinds.Set("partial", JsonValue::MakeString("Some forms print their own JSON (see the purpose); the others use the envelope."));
    kinds.Set("protocol", JsonValue::MakeString("The command speaks a JSON protocol on its standard streams; --json is ignored."));
    result.Set("jsonOutput", std::move(kinds));
    result.Set("note", JsonValue::MakeString("Options listed on a command apply to all of its subcommands; a subcommand lists its own. "
        "A usage line that offers alternatives lists their options as optional. Defaults are the values the usage text shows for an optional option."));

    auto commands = JsonValue::MakeArray();
    for (const CliCommandDescriptor& command : CliCommandCatalog()) commands.Append(CommandJson(command));
    result.Set("commands", std::move(commands));
    return result.Dump();
}

} // namespace kb::cli
