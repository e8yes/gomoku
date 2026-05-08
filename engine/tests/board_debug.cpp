#include "board.h"
#include <iostream>

int main() {
  Board board;
  board.Apply(0);
  board.Apply(1);
  board.Apply(2);
  board.Apply(Action::kSwap2ChooseWhite);
  
  board.Apply(15 * 1 + 0);
  board.Apply(15 * 0 + 1);
  board.Apply(15 * 2 + 0);
  board.Apply(15 * 0 + 2);
  board.Apply(15 * 3 + 0);
  board.Apply(15 * 0 + 3);
  board.Apply(15 * 4 + 0);
  board.Apply(15 * 0 + 4);
  
  std::cout << "Before 5th: result = " << (int)board.result() << std::endl;
  board.Apply(15 * 5 + 0);
  std::cout << "After 5th: result = " << (int)board.result() << std::endl;
  
  return 0;
}
