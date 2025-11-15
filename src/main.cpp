#include <iostream>
#include <string>
#include <cstring>
#include <ranges>
#include <vector>
#include <filesystem>
#include <fstream>

#include <readline/readline.h>

using std::string, std::vector;

#ifdef _WIN32
constexpr char path_list_sep = ';';
#else
constexpr char path_list_sep = ':';
#endif

namespace fs = std::filesystem;

class ArgsParser {
  string args_str;
public:
  vector<string> args;
  std::ostream *out_stream;
  std::ostream *err_stream;
  std::ofstream out_file;
  std::ofstream err_file;
  vector<string> args_trunc;

private:
  void parse_args() {
    args.emplace_back("");
    auto it = args_str.begin();
    bool ongoing_single_quote = false;
    bool ongoing_double_quote = false;
    do {
      if (*it == '\\' && !ongoing_single_quote && !ongoing_double_quote) {
        args[args.size() - 1] += *(++it);
        ++it;
      }
      else if (*it == '\\' && ongoing_double_quote) {
        ++it;
        if (*it != '\"' && *it != '\\' && *it != '$' && *it != '`')
          args[args.size() - 1] += '\\';
        args[args.size() - 1] += *it;
        ++it;
      }
      else if (*it == '\'' && !ongoing_double_quote) {
        ongoing_single_quote = !ongoing_single_quote;
        ++it;
      }
      else if (*it == '"' && !ongoing_single_quote) {
        ongoing_double_quote = !ongoing_double_quote;
        ++it;
      }
      else if (*it == ' ' && !ongoing_single_quote && !ongoing_double_quote) {
        args.emplace_back("");
        while (it != args_str.end() && *it == ' ')
          ++it;
      }
      else {
        args[args.size() - 1] += *it;
        ++it;
      }
    } while (it != args_str.end());
  }
  void set_streams() {
    auto redir_out_idx = std::min(
      std::ranges::find(args, ">"),
      std::ranges::find(args, "1>")
    );
    if (redir_out_idx < args.end() - 1) {
      out_file.open(*(redir_out_idx + 1));
      out_stream = &out_file;
    }
    const auto redir_out_append_idx = std::min(
      std::ranges::find(args, ">>"),
      std::ranges::find(args, "1>>")
    );
    if (redir_out_append_idx < args.end() - 1) {
      out_file.open(*(redir_out_append_idx + 1), std::ios::app);
      out_stream = &out_file;
    }
    const auto redir_err_idx = std::ranges::find(args, "2>");
    if (redir_err_idx < args.end() - 1) {
      err_file.open(*(redir_err_idx + 1));
      err_stream = &err_file;
    }
    const auto redir_err_append_idx = std::ranges::find(args, "2>>");
    if (redir_err_append_idx < args.end() - 1) {
      err_file.open(*(redir_err_append_idx + 1), std::ios::app);
      err_stream = &err_file;
    }

    auto end_idx = std::min(redir_out_idx, redir_out_append_idx);
    end_idx = std::min(end_idx, redir_err_idx);
    end_idx = std::min(end_idx, redir_err_append_idx);
    for (auto i = args.begin(); i < end_idx; ++i)
      args_trunc.emplace_back(*i);
  }
public:
  explicit ArgsParser(string args_str) : args_str(std::move(args_str)) {
    parse_args();
    out_stream = &(std::cout);
    err_stream = &(std::cerr);
    set_streams();

    *out_stream << std::unitbuf;
    *err_stream << std::unitbuf;
  }
};


string find_exe(const string &stem) {
  const string path = std::getenv("PATH");
  vector<string> pathParts;
  string pathPartCurr;
  for (const char pathChar: path) {
    if (pathChar == path_list_sep) {
      pathParts.push_back(pathPartCurr);
      pathPartCurr = "";
      continue;
    }
    pathPartCurr += pathChar;
  }
  pathParts.push_back(pathPartCurr);

  for (const auto& pathPart: pathParts) {
    if (!fs::exists(fs::path(pathPart)))
      continue;
    for (const auto & entry : fs::directory_iterator{pathPart}) {
      if ((entry.status().permissions() & fs::perms::owner_exec) == fs::perms::none)
        continue;
      if (entry.path().stem().string() == stem)
        return entry.path().string();
    }
  }
  return "";
}

void handle_echo(const ArgsParser &args_parser) {
  for (const auto &arg: args_parser.args_trunc | std::ranges::views::drop(1)) {
    *args_parser.out_stream << arg << ' ';
  }
  *args_parser.out_stream << '\n';
  // args_parser.out_stream->flush();
}

void handle_pwd(const ArgsParser &args_parser) {
  *args_parser.out_stream << fs::current_path().string() << '\n';
}

void handle_cd(const ArgsParser &args_parser) {
  string path_str = args_parser.args[1];
  if (const auto tilde_pos = path_str.find('~'); tilde_pos != string::npos) {
    const auto home_path_str = std::getenv("HOME");
    path_str = path_str.replace(tilde_pos, 1, home_path_str);
  }
  if (const fs::path cd_path(path_str); fs::exists(cd_path))
    fs::current_path(cd_path);
  else
    *args_parser.out_stream << "cd: " << path_str << ": No such file or directory\n";
}

void handle_type(const ArgsParser &args_parser) {
  const string &arg = args_parser.args[1];
  if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd") {
    *args_parser.out_stream << arg << " is a shell builtin\n";
    return;
  }

  if (const auto exe_path = find_exe(arg); exe_path.empty())
    *args_parser.out_stream << arg << ": not found\n";
  else
    *args_parser.out_stream << arg << " is " << exe_path << '\n';
}

string escape_special_chars(const string &in) {
  string result;
  for (const auto ch: in) {
    if (ch == ' ' || ch == '\'' || ch == '\"' || ch == '\\')
      result += '\\';
    result += ch;
  }
  return result;
}

static const char* const commands[] = {
  "echo",
  "exit",
  nullptr
};

static char* command_generator(const char* text, const int state) {
  static size_t list_index;
  static size_t len;

  if (state == 0) {
    list_index = 0;
    len = std::strlen(text);
  }

  const char* name;
  while ((name = commands[list_index++]) != nullptr) {
    if (strncmp(name, text, len) == 0) {
      return strdup(name);
    }
  }

  return nullptr;
}

char** character_name_completion(const char* text, const int start, const int end) {
  (void)start;
  (void)end;

  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, command_generator);
}

int main() {
  rl_attempted_completion_function = character_name_completion;

  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true) {
    const ArgsParser argsParser(readline("$ "));

    if (argsParser.args_trunc[0] == "exit") {
      int exit_status = 0;
      if (argsParser.args_trunc.size() > 1)
        exit_status = std::stoi(argsParser.args_trunc[1]);
      return exit_status;
    }

    if (argsParser.args_trunc[0] == "pwd")
      handle_pwd(argsParser);
    else if (argsParser.args_trunc[0] == "cd")
      handle_cd(argsParser);
    else if (argsParser.args_trunc[0] == "echo")
      handle_echo(argsParser);
    else if (argsParser.args_trunc[0] == "type")
      handle_type(argsParser);
    else if (!find_exe(argsParser.args_trunc[0]).empty()) {
      string exe_args = escape_special_chars(argsParser.args[0]);
      for (int i = 1; i < argsParser.args.size(); i++)
        exe_args += " " + escape_special_chars(argsParser.args[i]);
      std::system(exe_args.c_str());
    }
    else
      *argsParser.out_stream << argsParser.args[0] << ": command not found\n";
  }
}
