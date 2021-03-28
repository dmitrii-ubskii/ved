#include "ncursespp/ncurses.h"

#include "editor.h"

int main(int argc, char** argv)
{
	auto editor = Editor{};
	if (argc > 1)  // got a filename
	{
		editor.open(argv[1]);
	}
	return editor.mainLoop();
}
