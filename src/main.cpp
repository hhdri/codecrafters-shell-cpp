#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
constexpr char path_list_sep = ';';
#else
constexpr char path_list_sep = ':';
#endif


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
      if (arg == "exit" || arg == "echo" || arg == "type") {
        std::cout << arg << " is a shell builtin\n";
        continue;
      }
      std::string path = std::getenv("PATH");
      // std::cout << path << '\n';
      std::vector<std::string> pathParts;
      std::string pathPartCurr = "";
      for (char pathChar: path) {
        if (pathChar == path_list_sep) {
          pathParts.push_back(pathPartCurr);
          pathPartCurr = "";
        }
        else
          pathPartCurr += pathChar;
      }
      pathParts.push_back(pathPartCurr);

      bool isFound = false;
      for (auto pathPart: pathParts) {
        // std::cout << pathPart << '\n';
        if (! std::filesystem::exists(std::filesystem::path(pathPart)))
          continue;
        for (const auto & entry : std::filesystem::directory_iterator{pathPart}) {
          if ((entry.status().permissions() & std::filesystem::perms::owner_exec) == std::filesystem::perms::none)
            continue;
          if (entry.path().stem().string() == arg) {
            std::cout << arg << " is " << entry.path().string() << '\n';
            isFound = true;
            break;
          }
        }
        if (isFound)
          break;
      }
      if (!isFound)
        std::cout << arg << ": not found\n";
    }
     else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}
