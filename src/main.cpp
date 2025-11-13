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

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true) {
    std::cout << "$ ";
    std::string command;
    std::cin >> command;
    if (command == "exit") {
      int exitStatus;
      std::cin >> exitStatus;
      return exitStatus;
    }
    else if (command == "pwd") {
      std::cout << fs::current_path().string() << '\n';
    }
    else if (command == "echo") {
      std::string echoResult;
      std:getline(std::cin, echoResult);
      int startIdx = 0;
      while (echoResult[startIdx] == ' ')
        startIdx++;
      std::cout << echoResult.substr(startIdx, echoResult.length() - startIdx) << std::endl;
    }
    else if (command == "type") {
      std::string arg;
      std::cin >> arg;
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
      std::string exe_args;
      std::getline(std::cin, exe_args);
      std::system((command + std::string(" ") + exe_args).c_str());
    }
    else {
      std::cout << command << ": command not found\n";
    }
  }
}
