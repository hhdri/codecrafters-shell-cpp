#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
constexpr char path_list_sep = ';';
#else
constexpr char path_list_sep = ':';
#endif

namespace fs = std::filesystem;


std::string find_exe(const std::string &stem) {
  std::string path = std::getenv("PATH");
  std::vector<std::string> pathParts;
  std::string pathPartCurr;
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

void handle_echo(std::vector<std::string> &args) {
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

std::string escape_special_chars(const std::string &in) {
  std::string result;
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
    std::string args_str;
    std::getline(std::cin, args_str);
    std::vector<std::string> args;
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

    if (args[0] == "exit") {
      int exitStatus = std::stoi(args[1]);
      return exitStatus;
    }
    else if (args[0] == "pwd") {
      std::cout << fs::current_path().string() << '\n';
    }
    else if (args[0] == "cd") {
      std::string path_str = args[1];
      auto tilde_pos = path_str.find('~');
      if (tilde_pos != std::string::npos) {
        auto home_path_str = std::getenv("HOME");
        path_str = path_str.replace(tilde_pos, 1, home_path_str);
      }
      fs::path cd_path(path_str);
      if (fs::exists(cd_path))
        fs::current_path(cd_path);
      else
        std::cout << "cd: " << path_str << ": No such file or directory\n";
    }
    else if (args[0] == "echo")
      handle_echo(args);
    else if (args[0] == "type") {
      const std::string &arg = args[1];
      if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd") {
        std::cout << arg << " is a shell builtin\n";
        continue;
      }
      
      auto exe_path = find_exe(arg);
      if (exe_path.empty())
        std::cout << arg << ": not found\n";
      else
        std::cout << arg << " is " << exe_path << '\n';
    }
    else if (!find_exe(args[0]).empty()) {
      std::string exe_args = escape_special_chars(args[0]);
      for (int i = 1; i < args.size(); i++)
        exe_args += " " + escape_special_chars(args[i]);
      std::system(exe_args.c_str());
    }
    else {
      std::cout << args[0] << ": command not found\n";
    }
  }
}
