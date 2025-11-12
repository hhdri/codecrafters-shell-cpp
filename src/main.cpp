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
      else
        std::cout << arg << ": not found\n";
    }
     else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}
