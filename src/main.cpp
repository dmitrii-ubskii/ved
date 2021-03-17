#include "ncursespp/ncurses.h"

#include "editor.h"

int main()
{
	auto editor = Editor{};
	return editor.mainLoop();
}
