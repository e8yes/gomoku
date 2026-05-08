#include "board.h"
#include <iostream>

int main() {
  Board board;
  board.Apply(Action::FromXY(0, 0).id);
  board.Apply(Action::FromXY(1, 0).id);
  board.Apply(Action::FromXY(2, 0).id);
  board.Apply(Action::kSwap2ChooseWhite);
  
  board.Apply(Action::FromXY(0, 1).id);
  board.Apply(Action::FromXY(1, 0).id);
  board.Apply(Action::FromXY(0, 2).id);
  board.Apply(Action::FromXY(2, 0).id);
  board.Apply(Action::FromXY(0, 3).id);
  board.Apply(Action::FromXY(3, 0).id);
  board.Apply(Action::FromXY(0, 4).id);
  board.Apply(Action::FromXY(4, 0).id);
  
  std::cout << "Before 5th: result = " << (int)board.result() << std::endl;
  board.Apply(Action::FromXY(0, 5).id);
  std::cout << "After 5th: result = " << (int)board.result() << std::endl;
  
  return 0;
}
