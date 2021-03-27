#ifndef SRC_EDITOR_H_
#define SRC_EDITOR_H_

#include <string>
#include <vector>

#include "ncursespp/geometry.h"
#include "ncursespp/ncurses.h"
#include "ncursespp/window.h"

class Editor
{
public:
	Editor();

	int mainLoop();

private:
	void handleKey(ncurses::Key);
	void repaint();

	ncurses::Ncurses context;

	ncurses::Window editorWindow;
	ncurses::Window statusLine;

	int topLine = 0;
	bool wrap = false;

	class Buffer
	{
	public:
		void erase(int line, int col, int count);
		void insert(int line, int col, char);
		void breakLine(int line, int col);

		int length() const;
		int numLines() const;

		bool is_empty() const;

		std::string const& getLine(int idx) const;

	private:
		std::vector<std::string> lines{""};
	} buffer;

	ncurses::Point cursorPosition {0, 0};
};

#endif // SRC_EDITOR_H_
