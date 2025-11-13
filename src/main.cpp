#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
constexpr char path_list_sep = ';';
#else
constexpr char path_list_sep = ':';
#endif

namespace fs = std::filesystem;


std::string find_exe(std::string &stem) {
  std::string path = std::getenv("PATH");
  std::vector<std::string> pathParts;
  std::string pathPartCurr = "";
  for (char pathChar: path) {
    if (pathChar == path_list_sep) {
      pathParts.push_back(pathPartCurr);
      pathPartCurr = "";
      continue;
    }
    pathPartCurr += pathChar;
  }
  pathParts.push_back(pathPartCurr);

  for (auto pathPart: pathParts) {
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

std::string resolve_escape_chars(std::string in) {
  int pos;
  while((pos = in.find("\\ ")) != std::string::npos)
    in = in.replace(pos, 2, " ");
  while((pos = in.find("\\'")) != std::string::npos)
    in = in.replace(pos, 2, "'");
    return in;
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
    args.push_back("");
    auto it = args_str.begin();
    bool ongoing_single_quote = false;
    bool ongoing_double_quote = false;
    do {
      if (*it == '\'' && !ongoing_double_quote) {
        ongoing_single_quote = !ongoing_single_quote;
        it++;
      }
      else if (*it == '"') {
        ongoing_double_quote = !ongoing_double_quote;
        it++;
      }
      else if (*it == ' ' && !ongoing_single_quote && !ongoing_double_quote) {
        args.push_back("");
        while (*it == ' ' && it != args_str.end())
          it++;
      }
      else {
        if (*it == ' ' || *it == '\'')
          args[args.size() - 1] += '\\';
        args[args.size() - 1] += *it;
        it++;
      }
    } while (it != args_str.end());
    auto command = args[0];

    if (command == "exit") {
      int exitStatus = std::stoi(args[1]);
      return exitStatus;
    }
    else if (command == "pwd") {
      std::cout << fs::current_path().string() << '\n';
    }
    else if (command == "cd") {
      std::string path_str = args[1];
      auto tilde_pos = path_str.find("~");
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
    else if (command == "echo") {
      for (int i = 1; i < args.size(); i++)
        std::cout << resolve_escape_chars(args[i]) << ' ';
      std::cout << '\n';
    }
    else if (command == "type") {
      std::string arg = args[1];
      if (arg == "exit" || arg == "echo" || arg == "type" || arg == "pwd") {
        std::cout << arg << " is a shell builtin\n";
        continue;
      }
      
      auto exe_path = find_exe(arg);
      if (exe_path == "")
        std::cout << arg << ": not found\n";
      else
        std::cout << arg << " is " << exe_path << '\n';
    }
    else if (find_exe(command) != "") {
      std::string exe_args = args[0];
      for (int i = 1; i < args.size(); i++)
        exe_args += " " + args[i];
      std::system(exe_args.c_str());
    }
    else {
      std::cout << command << ": command not found\n";
    }
  }
}
