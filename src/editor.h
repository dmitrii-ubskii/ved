#ifndef SRC_EDITOR_H_
#define SRC_EDITOR_H_

#include <filesystem>
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
	void open(std::filesystem::path);

private:
	void handleKey(ncurses::Key);
	void repaint();

	ncurses::Ncurses context;

	ncurses::Window editorWindow;
	ncurses::Window lineNumbers;
	ncurses::Window statusLine;

	ncurses::Point getScreenCursorPosition() const;

	std::size_t getLineLength(std::string_view lineContents) const;
	std::size_t getLineVirtualHeight(std::string_view lineContents) const;

	int topLine = 0;
	int leftCol = 0;
	bool wrap = false;

	class Buffer
	{
	public:
		void erase(int line, int col, int count);
		void insert(int line, int col, char);
		void breakLine(int line, int col);
		void joinLines(int line, int n);

		int length() const;
		int numLines() const;

		bool is_empty() const;
		void read(std::filesystem::path);

		std::string const& getLine(int idx) const;

	private:
		std::vector<std::string> lines{""};
	} buffer;

	enum class Mode
	{
		Normal, Insert
	} mode = Mode::Normal;

	int cursorLine{0};
	int cursorCol{0};
};

#endif // SRC_EDITOR_H_
