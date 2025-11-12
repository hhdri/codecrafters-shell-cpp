#include <iostream>
#include <string>

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
      if (arg == "exit" || arg == "echo" || arg == "type")
        std::cout << arg << " is a shell builtin\n";
      std::string path = std::getenv("PATH");
      std::cout << path << '\n';
      std::vector<std::string> pathParts;
      int startIdx = 0, length = 0;
      for (int i = 0; i < path.length(); i++) {
        char curr = path[i];
        if (curr != ':') {
          length++;
          if (i != path.length() - 1)
            continue;
        }
        if (length != 0) {
          pathParts.push_back(path.substr(startIdx, length));
          startIdx = i + 1;
          length = 0;
        }
      }
      for (auto pathPart: pathParts)
        std::cout << pathPart << '\n';
      //else
      //  std::cout << arg << ": not found\n";
    }
     else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}
