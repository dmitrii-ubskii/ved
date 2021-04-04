#include "editor.h"

#include <cctype>
#include <fstream>
#include <numeric>
#include <string>

#include "ncursespp/color.h"

void Editor::Buffer::erase(CursorPosition p, int count)
{
	lines[p.line].erase(p.col, count);
}

void Editor::Buffer::insert(CursorPosition p, char ch)
{
	lines[p.line].insert(p.col, 1, ch);
}

void Editor::Buffer::insertLine(int line)
{
	lines.insert(lines.begin() + line + 1, "");
}

void Editor::Buffer::breakLine(CursorPosition p)
{
	std::size_t colIndex = p.col;
	if (lines[p.line].length() == colIndex)
	{
		lines.emplace(lines.begin() + p.line + 1);
	}
	else
	{
		auto tail = lines[p.line].substr(colIndex);
		lines[p.line] = lines[p.line].substr(0, colIndex);
		lines.emplace(lines.begin() + p.line + 1, tail);
	}
}

void Editor::Buffer::joinLines(int line, int count)
{
	if (count <= 1)  // nothing to be done
	{
		return;
	}
	lines[line] = std::accumulate(
		lines.cbegin() + line,
		lines.cbegin() + line + count,
		std::string{}, [](auto const& acc, auto const& lhs) { return acc + lhs; }
	);
	lines.erase(lines.cbegin() + line + 1, lines.cbegin() + line + count);
}

int Editor::Buffer::length() const
{
	return std::accumulate(
		lines.begin(), lines.end(), 0,
		[](auto lhs, auto& s) { return lhs + s.length(); }
	) + lines.size() - 1;
}

int Editor::Buffer::numLines() const
{
	return lines.size();
}

bool Editor::Buffer::is_empty() const
{
	return numLines() == 0;
}

void Editor::Buffer::read(std::filesystem::path filePath)
{
	lines.clear();
	auto fileHandler = std::ifstream(filePath);
	auto lineBuffer = std::string{};
	while (not fileHandler.eof())
	{
		std::getline(fileHandler, lineBuffer);
		lines.push_back(lineBuffer);
	}
}

int Editor::Buffer::lineLength(int idx) const
{
	return lines[idx].length();
}

std::string const& Editor::Buffer::getLine(int idx) const
{
	return lines[idx];
}

// *** //

struct OperatorArgs
{
	ncurses::Key const key;
	Editor::Buffer& buffer;
	CursorPosition const cursor;
	WindowInfo const windowInfo;
	Editor::Mode const currentMode;
};

struct OperatorResult
{
	bool cursorMoved{false};
	CursorPosition cursorPosition{0, 0};
	bool bufferChanged{false};
	bool modeChanged{false};
	Editor::Mode newMode{Editor::Mode::Normal};
	// yank contents
};

using OperatorFunction = OperatorResult(*)(OperatorArgs args);

[[nodiscard]] OperatorResult moveCursor(OperatorArgs args)
{
	auto cursor = args.cursor;

	auto lastValidOffset = args.currentMode == Editor::Mode::Insert ? 0 : -1;

	switch (args.key)
	{
		case ' ':
		case ncurses::Key::Right:
			if (cursor.col < args.buffer.lineLength(cursor.line) + lastValidOffset)
			{
				cursor.col++;
			}
			else if (cursor.line < args.buffer.numLines() - 1)
			{
				cursor.line++;
				cursor.col = 0;
			}
			break;

		case ncurses::Key::Backspace:
		case ncurses::Key::Left:
			if (cursor.col > 0)
			{
				cursor.col--;
			}
			else if (cursor.line > 0)
			{
				cursor.line--;
				cursor.col = std::max(0, args.buffer.lineLength(cursor.line) + lastValidOffset);
			}
			
			break;

		case '$':
		case ncurses::Key::End:
			cursor.col = std::max(0, args.buffer.lineLength(cursor.line) + lastValidOffset);
			break;

		default:
			throw;
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}

[[nodiscard]] OperatorResult scrollBuffer(OperatorArgs args)
{
	auto cursor = args.cursor;
	switch (args.key)
	{
		case 'g':
			cursor.line = args.buffer.numLines() - 1;  // FIXME go to line #COUNT
			break;

		case 'h':
			cursor.line = args.windowInfo.topLine;
			break;

		case 'l':
			// FIXME get actual lowest visible line
			cursor.line = std::min(args.windowInfo.topLine + 22, args.buffer.numLines() - 1);
			break;

		case 'b':
			cursor.line = 0;
			break;

		case ncurses::Key::Down:
			if (cursor.line < args.buffer.numLines() - 1)
			{
				cursor.line++;
			}
			break;

		case ncurses::Key::Up:
			if (cursor.line > 0)
			{
				cursor.line--;
			}
			break;

		default:
			throw;
	}
	int cursorLineLength = args.buffer.lineLength(cursor.line);
	if (cursor.col > cursorLineLength)
	{
		if (cursorLineLength == 0)
		{
			cursor.col = 0;
		}
		else
		{
			cursor.col = cursorLineLength - 1;
		}
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}


[[nodiscard]] OperatorResult moveToStartOfLine(OperatorArgs args)
{
	auto cursor = args.cursor;
	cursor.col = 0;
	switch (args.key)
	{
		case ncurses::Key::Enter:
			if (cursor.line < args.buffer.numLines() - 1)
			{
				cursor.line++;
			}
			break;

		case '-':
			if (cursor.line > 0)
			{
				cursor.line--;
			}
			break;

		case '0':
		case ncurses::Key::Home:
			/* stay on this line */
			break;

		default:
			throw;
	}

	return {.cursorMoved=true, .cursorPosition=cursor};
}

[[nodiscard]] OperatorResult deleteChars(OperatorArgs args)
{
	auto result = OperatorResult{.bufferChanged=true};
	switch (args.key)
	{
		case 'x':
			if (args.buffer.lineLength(args.cursor.line) == 0)
			{
				result.bufferChanged = false;
			}
			else
			{
				args.buffer.erase(args.cursor, 1);
				int cursorLineLength = args.buffer.lineLength(args.cursor.line);
				if (cursorLineLength == 0)
				{
					result.cursorMoved = true;
					result.cursorPosition = {.line=args.cursor.line, .col=0};
				}
				else if (args.cursor.col >= cursorLineLength)
				{
					result.cursorMoved = true;
					result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col - 1};
				}
			}
			break;

		default:
			throw;
	}
	return result;
}

[[nodiscard]] OperatorResult startInsert(OperatorArgs args)
{
	OperatorResult result{.modeChanged=true, .newMode=Editor::Mode::Insert};

	switch (args.key)
	{
		case 'i':
			break;

		case 'a':
			if (args.buffer.lineLength(args.cursor.line) > 0)
			{
				// if the line is not empty, we're guaranteed that the column after the cursor is a valid spot
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col + 1};
			}
			break;

		case 'o':
			args.buffer.insertLine(args.cursor.line);
			result.bufferChanged = true;
			result.cursorMoved = true;
			result.cursorPosition = {.line=args.cursor.line + 1, .col=0};
			break;

		default:
			throw;
	}

	return result;
}

[[nodiscard]] OperatorResult startNormal(OperatorArgs args)
{
	return {
		.cursorMoved=true, .cursorPosition={args.cursor.line, std::max(args.cursor.col-1, 0)},
		.modeChanged=true, .newMode=Editor::Mode::Normal
	};
}

auto normalOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	{ncurses::Key{' '}, moveCursor},
	{ncurses::Key::Right, moveCursor},
	{ncurses::Key::Backspace, moveCursor},
	{ncurses::Key::Left, moveCursor},
	{ncurses::Key{'$'}, moveCursor},
	{ncurses::Key::End, moveCursor},
	{ncurses::Key{'g'}, scrollBuffer},
	{ncurses::Key{'h'}, scrollBuffer},
	{ncurses::Key{'l'}, scrollBuffer},
	{ncurses::Key{'b'}, scrollBuffer},
	{ncurses::Key::Down, scrollBuffer},
	{ncurses::Key::Up, scrollBuffer},
	{ncurses::Key::Enter, moveToStartOfLine},
	{ncurses::Key{'-'}, moveToStartOfLine},
	{ncurses::Key{'0'}, moveToStartOfLine},
	{ncurses::Key::Home, moveToStartOfLine},
	{ncurses::Key{'x'}, deleteChars},
	// TODO not implemented: operator-pending commands
	// {ncurses::Key{'r'}, replaceChars},  // any character
	// {ncurses::Key{'d'}, deleteLines},   // dd or dy (delete / cut)
	// {ncurses::Key{'y'}, yankLines},     // yy or yd (yank / cut)
	{ncurses::Key{'i'}, startInsert},
	{ncurses::Key{'a'}, startInsert},
	{ncurses::Key{'o'}, startInsert},
	// TODO not implemented: Command mode
	// {ncurses::Key{'/'}, startCommand},
	// {ncurses::Key{':'}, startCommand},
	// {ncurses::Key{'z'}, redraw},
};

auto insertOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	{ncurses::Key::Right, moveCursor},
	{ncurses::Key::Left, moveCursor},
	{ncurses::Key::Down, scrollBuffer},
	{ncurses::Key::Up, scrollBuffer},
	{ncurses::Key::End, moveCursor},
	{ncurses::Key::Home, moveToStartOfLine},
	{ncurses::Key::Escape, startNormal},
	// {ncurses::Key::Backspace, ...},
	// {ncurses::Key::Enter, ...},
};

Editor::Editor()
	: context{}
	, editorWindow{{{4, 0}, {0, context.get_rect().s.h - 1}}}
	, lineNumbers({{0, 0}, {4, context.get_rect().s.h - 1}})
	, statusLine{{{0, context.get_rect().s.h - 1}, {}}}
{
	context.raw(true);
	editorWindow.setbackground(ncurses::Color::Yellow, ncurses::Color::Blue);
	editorWindow.setcolor(ncurses::Color::Yellow, ncurses::Color::Blue);
	lineNumbers.setbackground(ncurses::Color::Gray, ncurses::Color::Purple);
	lineNumbers.setcolor(ncurses::Color::Gray, ncurses::Color::Purple);
	context.refresh();
	repaint();
}

void Editor::open(std::filesystem::path path)
{
	buffer.read(path);
	repaint();
}

void Editor::handleKey(ncurses::Key k)
{
	switch (mode)
	{
		case Mode::Normal:
			if (normalOps.contains(k))
			{
				auto res = normalOps[k]({k, buffer, cursor, windowInfo, mode});
				auto needToRepaint = res.bufferChanged;
				if (res.cursorMoved)
				{
					cursor = res.cursorPosition;
					if (windowInfo.topLine > cursor.line)
					{
						windowInfo.topLine = cursor.line;
						needToRepaint = true;
					}
					while (getScreenCursorPosition().y >= editorWindow.get_rect().s.h)
					{
						windowInfo.topLine++;
						needToRepaint = true;
					}
					if (not wrap)
					{
						while (cursor.col - windowInfo.leftCol >= editorWindow.get_rect().s.w)
						{
							windowInfo.leftCol += 20;
							needToRepaint = true;
						}
						while (windowInfo.leftCol > cursor.col)
						{
							windowInfo.leftCol -= 20;
							needToRepaint = true;
						}
					}
					editorWindow.move(getScreenCursorPosition());
				}
				if (res.modeChanged)
				{
					mode = res.newMode;
					needToRepaint = true;
				}
				if (needToRepaint)
				{
					repaint();
				}
			}
			break;

		case Mode::Insert:
			if (insertOps.contains(k))
			{
				auto res = insertOps[k]({k, buffer, cursor, windowInfo, mode});
				auto needToRepaint = res.bufferChanged;
				if (res.cursorMoved)
				{
					cursor = res.cursorPosition;
					if (windowInfo.topLine > cursor.line)
					{
						windowInfo.topLine = cursor.line;
						needToRepaint = true;
					}
					while (getScreenCursorPosition().y >= editorWindow.get_rect().s.h)
					{
						windowInfo.topLine++;
						needToRepaint = true;
					}
					if (not wrap)
					{
						while (cursor.col - windowInfo.leftCol >= editorWindow.get_rect().s.w)
						{
							windowInfo.leftCol += 20;
							needToRepaint = true;
						}
						while (windowInfo.leftCol > cursor.col)
						{
							windowInfo.leftCol -= 20;
							needToRepaint = true;
						}
					}
					editorWindow.move(getScreenCursorPosition());
				}
				if (res.modeChanged)
				{
					mode = res.newMode;
					needToRepaint = true;
				}
				if (needToRepaint)
				{
					repaint();
				}
			}
			else
			{
				auto ch = static_cast<int>(k);
				if (ch < 256 && std::isprint(ch))
				{
					buffer.insert(cursor, static_cast<char>(ch));
					cursor.col++;
				}
				repaint();
			}
			break;
	}
}

void Editor::repaint()
{
	editorWindow.erase();
	lineNumbers.erase();
	auto lineY = 0;
	for (auto i = windowInfo.topLine; i < buffer.numLines(); i++)
	{
		if (wrap)
		{
			editorWindow.mvaddstr({0, lineY}, buffer.getLine(i));
		}
		else
		{
			if (buffer.lineLength(i) > windowInfo.leftCol)
			{
				editorWindow.mvaddnstr({0, lineY}, buffer.getLine(i).substr(windowInfo.leftCol), editorWindow.get_rect().s.w);
			}
		}
		auto lineNumberString = std::to_string(i + 1);
		int x = 3 - lineNumberString.length();
		lineNumbers.mvaddnstr({x, lineY}, lineNumberString, lineNumbers.get_rect().s.w-1);
		lineY += getLineVirtualHeight(buffer.getLine(i));
	}
	for (auto y = lineY; y < editorWindow.get_rect().s.h; y++)
	{
		editorWindow.mvaddstr({0, y}, "~");
	}

	lineNumbers.refresh();

	statusLine.erase();
	switch (mode)
	{
		case Mode::Normal:
			statusLine.mvaddstr({0,0}, "-- NORMAL --");
			break;

		case Mode::Insert:
			statusLine.mvaddstr({0,0}, "-- INSERT --");
			break;
	}
	statusLine.refresh();

	editorWindow.refresh();
	editorWindow.move(getScreenCursorPosition());
}

std::size_t Editor::getLineLength(std::string_view lineContents) const
{
	return lineContents.length();
}

std::size_t Editor::getLineVirtualHeight(std::string_view lineContents) const
{
	if (not wrap)
	{
		return 1;
	}
	return (lineContents.length() + 1) / editorWindow.get_rect().s.w + 1;
}

ncurses::Point Editor::getScreenCursorPosition() const
{
	ncurses::Point pos{0, 0};
	for (auto i = windowInfo.topLine; i < cursor.line; i++)
	{
		pos.y += getLineVirtualHeight(buffer.getLine(i));
	}

	if (wrap)
	{
		pos.x = cursor.col % editorWindow.get_rect().s.w;
		pos.y += cursor.col / editorWindow.get_rect().s.w;
	}
	else
	{
		pos.x = cursor.col - windowInfo.leftCol;
	}
	return pos;
}

int Editor::mainLoop()
{
	while (true)
	{
		auto ch = editorWindow.getch();
		if (ch == ncurses::Key::Ctrl({'c'}))  // Ctrl+C
		{
			break;
		}
		handleKey(ch);
	}
	return 0;
}
