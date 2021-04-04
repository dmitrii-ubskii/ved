#ifndef SRC_EDITOR_H_
#define SRC_EDITOR_H_

#include <filesystem>
#include <string>
#include <vector>

#include "ncursespp/geometry.h"
#include "ncursespp/ncurses.h"
#include "ncursespp/window.h"

struct CursorPosition
{
	int line;
	int col;
};

struct WindowInfo
{
	int topLine;
	int leftCol;
};

class Editor
{
public:
	Editor();

	int mainLoop();
	void open(std::filesystem::path);

	class Buffer
	{
	public:
		void erase(CursorPosition, int count);
		void insert(CursorPosition, char);
		void insertLine(int line);
		void breakLine(CursorPosition);
		void joinLines(int line, int count);

		int length() const;
		int numLines() const;

		bool is_empty() const;
		void read(std::filesystem::path);

		int lineLength(int idx) const;
		std::string const& getLine(int idx) const;

	private:
		std::vector<std::string> lines{""};
	};

	enum class Mode
	{
		Normal, Insert, Command
	};

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

	WindowInfo windowInfo{.topLine=0, .leftCol=0};
	bool wrap = false;

	Buffer buffer;
	Mode mode = Mode::Normal;

	std::string cmdline;
	int cmdlineCursor{0};

	CursorPosition cursor{0, 0};
};

#endif // SRC_EDITOR_H_
