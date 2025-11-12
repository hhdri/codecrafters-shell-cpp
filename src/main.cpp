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
      // std::string echoPiece;
      // do {
      //   std::cin >> echoPiece;
      //   echoResult += " " + echoPiece;
      // } while (echoPiece != "\n");
      std::cout << echoResult << std::endl;
    }
     else {
      std::cout << command << ": command not found" << std::endl;
    }
  }
}
