#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

using std::string, std::vector;

#ifdef _WIN32
constexpr char path_list_sep = ';';
#else
constexpr char path_list_sep = ':';
#endif

namespace fs = std::filesystem;

vector<string> parse_args(const string &args_str) {
  vector<string> args;
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
      while (*it == ' ' && it != args_str.end())
        ++it;
    }
    else {
      args[args.size() - 1] += *it;
      ++it;
    }
  } while (it != args_str.end());
  return args;
}


string find_exe(const string &stem) {
  string path = std::getenv("PATH");
  vector<string> pathParts;
  string pathPartCurr;
  for (char pathChar: path) {
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

void handle_echo(const vector<string> &args) {
  auto output_ostream = &(std::cout);
  std::ofstream output_file;

  auto stdout_pipe_idx = std::find(args.begin(), args.end(), ">");
  stdout_pipe_idx = std::min(stdout_pipe_idx, std::find(args.begin(), args.end(), "1>"));
  if (stdout_pipe_idx < args.end() - 1) {
    output_file.open(*(stdout_pipe_idx + 1));
    output_ostream = &output_file;
  }
  for (int i = 1; i < stdout_pipe_idx - args.begin(); i++) {
    *output_ostream << args[i] << ' ';
  }
  *output_ostream << '\n';
}

void handle_pwd() {
  std::cout << fs::current_path().string() << '\n';
}

void handle_cd(const vector<string> &args) {
  string path_str = args[1];
  if (const auto tilde_pos = path_str.find('~'); tilde_pos != string::npos) {
    const auto home_path_str = std::getenv("HOME");
    path_str = path_str.replace(tilde_pos, 1, home_path_str);
  }
  if (const fs::path cd_path(path_str); fs::exists(cd_path))
    fs::current_path(cd_path);
  else
    std::cout << "cd: " << path_str << ": No such file or directory\n";
}

void handle_type(const vector<string> &args) {
  const string &arg = args[1];
  if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd") {
    std::cout << arg << " is a shell builtin\n";
    return;
  }

  if (const auto exe_path = find_exe(arg); exe_path.empty())
    std::cout << arg << ": not found\n";
  else
    std::cout << arg << " is " << exe_path << '\n';
}

string escape_special_chars(const string &in) {
  string result;
  for (auto ch: in) {
    if (ch == ' ' || ch == '\'' || ch == '\"' || ch == '\\')
      result += '\\';
    result += ch;
  }
  return result;
}

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true) {
    std::cout << "$ ";
    string args_str;
    std::getline(std::cin, args_str);
    const vector<string> args = parse_args(args_str);

    if (args[0] == "exit") {
      int exit_status = 0;
      if (args.size() > 1)
        exit_status = std::stoi(args[1]);
      return exit_status;
    }

    if (args[0] == "pwd")
      handle_pwd();
    else if (args[0] == "cd")
      handle_cd(args);
    else if (args[0] == "echo")
      handle_echo(args);
    else if (args[0] == "type")
      handle_type(args);
    else if (!find_exe(args[0]).empty()) {
      string exe_args = escape_special_chars(args[0]);
      for (int i = 1; i < args.size(); i++)
        exe_args += " " + escape_special_chars(args[i]);
      std::system(exe_args.c_str());
    }
    else
      std::cout << args[0] << ": command not found\n";
  }
}
