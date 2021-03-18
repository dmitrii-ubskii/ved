#include "editor.h"

#include <curses.h>
#include <string>
#undef getch
#undef mvaddstr

Editor::Editor()
	: context{}
	, editorWindow{{{0, 0}, {0, context.get_rect().s.h - 1}}}
	, statusLine{{{0, context.get_rect().s.h - 1}, {}}}
{
	context.refresh();
	repaint();
}

void Editor::handleKey(int ch)
{
	// FIXME move keycodes into an enum in ncursespp
	switch (ch)
	{
		case KEY_BACKSPACE:
			buffer.pop_back();
			break;

		default:
			buffer.push_back(static_cast<char>(ch));
			break;
	}

	repaint();
}

void Editor::repaint()
{
	editorWindow.clear();
	editorWindow.mvaddstr({0, 0}, buffer);
	refresh();
}

int Editor::mainLoop()
{
	while (true)
	{
		// FIXME shouldn't this work with context.getch() as well?
		// Something funky is afoot.
		auto ch = editorWindow.getch();
		if (ch == '\\') { break; }
		handleKey(ch);
	}
	return 0;
}
