#ifndef SRC_EDITOR_H_
#define SRC_EDITOR_H_

#include <string>

#include "ncursespp/ncurses.h"
#include "ncursespp/window.h"

class Editor
{
public:
	Editor();

	int mainLoop();

private:
	void handleKey(int);
	void repaint();

	ncurses::Ncurses context;

	ncurses::Window editorWindow;
	ncurses::Window statusLine;

	std::string buffer;
};

#endif // SRC_EDITOR_H_
