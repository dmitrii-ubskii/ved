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

void Editor::Buffer::yankTo(Register& r, int line, int count) const
{
	r.lines.clear();
	count = std::min(count, numLines() - line);
	r.lines.insert(r.lines.end(), lines.begin() + line, lines.begin() + line + count);
}

void Editor::Buffer::putFrom(Register const& r, int line)
{
	lines.insert(lines.begin() + line + 1, r.lines.cbegin(), r.lines.cend());
}

void Editor::Buffer::deleteLines(int line, int count)
{
	if (count < 1)  // nothing to be done
	{
		return;
	}
	count = std::min(count, numLines() - line);
	lines.erase(lines.cbegin() + line, lines.cbegin() + line + count);
	if (lines.empty())
	{
		lines.emplace_back("");
	}
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
	Editor::Register& reg;

	CursorPosition const cursor;
	WindowInfo const windowInfo;
	Editor::Mode const currentMode;

	ncurses::Key pendingOperator{ncurses::Key::Null};
	std::optional<int> const count{};
};

struct OperatorResult
{
	bool cursorMoved{false};
	CursorPosition cursorPosition{0, 0};

	bool bufferChanged{false};

	bool modeChanged{false};
	Editor::Mode newMode{Editor::Mode::Normal};

	std::string message{""};

	ncurses::Key pendingOperator{ncurses::Key::Null};
	std::optional<int> count{std::nullopt};
};

using OperatorFunction = OperatorResult(*)(OperatorArgs args);

[[nodiscard]] OperatorResult moveCursor(OperatorArgs args)
{
	auto cursor = args.cursor;

	auto lastValidOffset = [&](int index)
	{
		auto lineLength = args.buffer.lineLength(index);
		if (lineLength == 0)
			return 0;
		return lineLength + (args.currentMode == Editor::Mode::Insert ? 0 : -1);
	};

	auto toMove = args.count.value_or(1);

	switch (args.key)
	{
		case ' ':
		case ncurses::Key::Right:
			while (toMove)
			{
				auto lastValidPosOnLine = lastValidOffset(cursor.line);
				if (cursor.col + toMove <= lastValidPosOnLine)
				{
					cursor.col += toMove;
					break;
				}
				else if (cursor.line < args.buffer.numLines() - 1)
				{
					toMove -= lastValidPosOnLine - cursor.col + 1;
					cursor.col = 0;
					cursor.line++;
				}
				else
				{
					cursor.col = lastValidPosOnLine;
					break;
				}
			}
			break;

		case ncurses::Key::Backspace:
		case ncurses::Key::Left:
			while (toMove)
			{
				if (cursor.col >= toMove)
				{
					cursor.col -= toMove;
					break;
				}
				else if (cursor.line > 0)
				{
					toMove -= cursor.col + 1;
					cursor.line--;
					cursor.col = std::max(0, lastValidOffset(cursor.line));
				}
				else
				{
					cursor.col = 0;
					break;
				}
			}
			
			break;

		case '$':
		case ncurses::Key::End:
			cursor.col = std::max(0, lastValidOffset(cursor.line));
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
			cursor.line = std::min(args.count.value_or(args.buffer.numLines()) - 1, args.buffer.numLines() - 1);
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
			cursor.line = std::min(cursor.line + args.count.value_or(1), args.buffer.numLines() - 1);
			break;

		case ncurses::Key::Up:
			cursor.line = std::max(cursor.line - args.count.value_or(1), 0);
			break;

		default:
			throw;
	}
	int cursorLineLength = args.buffer.lineLength(cursor.line);
	if (cursor.col > cursorLineLength)
	{
		cursor.col = std::max(0, cursorLineLength - 1);
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
			cursor.line = std::min(cursor.line + args.count.value_or(1), args.buffer.numLines() - 1);
			break;

		case '-':
			cursor.line = std::max(cursor.line - args.count.value_or(1), 0);
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

[[nodiscard]] OperatorResult handleDigit(OperatorArgs args)
{
	switch (args.key)
	{
		case '0':
			if (not args.count.has_value())
			{
				return moveToStartOfLine(args);
			}
			[[fallthrough]];

		case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			return {
				.pendingOperator=args.pendingOperator,
				.count=args.count.value_or(0) * 10 + args.key.keycode - '0'
			};
			break;

		default:
			throw;
	}
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
				args.buffer.erase(args.cursor, args.count.value_or(1));
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

		case ncurses::Key::Backspace:
			if (args.cursor.col > 0)
			{
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line, .col=args.cursor.col - 1};
				args.buffer.erase(result.cursorPosition, 1);
			}
			else if (args.cursor.line > 0)
			{
				result.cursorMoved = true;
				result.cursorPosition = {.line=args.cursor.line - 1, .col=args.buffer.lineLength(args.cursor.line - 1)};
				args.buffer.joinLines(result.cursorPosition.line, 2);
			}
			break;

		default:
			throw;
	}
	return result;
}

[[nodiscard]] OperatorResult breakLine(OperatorArgs args)
{
	args.buffer.breakLine(args.cursor);
	return {.cursorMoved=true, .cursorPosition={.line=args.cursor.line + 1, .col=0}, .bufferChanged=true};
}

[[nodiscard]] OperatorResult deleteLines(OperatorArgs args)
{
	OperatorResult result{.bufferChanged=true};

	auto count = args.count.value_or(1);
	switch (args.key)
	{
		case 'd':  // dd
			break;

		case 'y':  // dy
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			break;

		default:
			throw;
	}
	args.buffer.deleteLines(args.cursor.line, count);
	result.message = std::to_string(count) + " fewer lines";

	result.cursorPosition = args.cursor;
	if (result.cursorPosition.line >= args.buffer.numLines())
	{
		result.cursorMoved = true;
		result.cursorPosition.line = args.buffer.numLines() - 1;
	}
	if (result.cursorPosition.col >= args.buffer.lineLength(result.cursorPosition.line))
	{
		result.cursorMoved = true;
		result.cursorPosition.col = std::max(0, args.buffer.lineLength(result.cursorPosition.line) - 1);
	}

	return result;
}

[[nodiscard]] OperatorResult yankLines(OperatorArgs args)
{
	auto count = args.count.value_or(1);
	switch (args.key)
	{
		case 'd':  // yd
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			// NOTE deleteLines() assumes pendingOperator == 'd', so it's equivalent to 'dd'
			return deleteLines(args);

		case 'y':  // yy
			args.buffer.yankTo(args.reg, args.cursor.line, count);
			break;

		default:
			throw;
	}
	return {.message=std::to_string(count) + " lines yanked"};
}

[[nodiscard]] OperatorResult doPendingOperator(OperatorArgs args)
{
	if (args.pendingOperator == ncurses::Key::Null)
	{
		return {.pendingOperator=args.key, .count=args.count};
	}

	switch (args.pendingOperator)
	{
		case 'd':
			return deleteLines(args);

		case 'y':
			return yankLines(args);

		default:
			throw;
	}
}

[[nodiscard]] OperatorResult putLines(OperatorArgs args)
{
	switch (args.key)
	{
		case 'p':
			args.buffer.putFrom(args.reg, args.cursor.line);
			return {.cursorMoved=true, .cursorPosition={args.cursor.line+1, 0}, .bufferChanged=true};

		case 'P':
			args.buffer.putFrom(args.reg, args.cursor.line-1);
			return {.cursorMoved=true, .cursorPosition={args.cursor.line, 0}, .bufferChanged=true};

		default:
			throw;
	}
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

[[nodiscard]] OperatorResult startCommand(OperatorArgs)
{
	return {
		.modeChanged=true, .newMode=Editor::Mode::Command
	};
}

[[nodiscard]] OperatorResult startNormal(OperatorArgs args)
{
	return {
		.cursorMoved=true, .cursorPosition={args.cursor.line, std::max(args.cursor.col-1, 0)},
		.modeChanged=true, .newMode=Editor::Mode::Normal
	};
}

auto normalOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	{ncurses::Key{'0'}, handleDigit},
	{ncurses::Key{'1'}, handleDigit},
	{ncurses::Key{'2'}, handleDigit},
	{ncurses::Key{'3'}, handleDigit},
	{ncurses::Key{'4'}, handleDigit},
	{ncurses::Key{'5'}, handleDigit},
	{ncurses::Key{'6'}, handleDigit},
	{ncurses::Key{'7'}, handleDigit},
	{ncurses::Key{'8'}, handleDigit},
	{ncurses::Key{'9'}, handleDigit},
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
	{ncurses::Key::Home, moveToStartOfLine},
	{ncurses::Key{'x'}, deleteChars},
	// TODO not implemented: operator-pending commands
	// {ncurses::Key{'r'}, replaceChars},  // any character
	{ncurses::Key{'d'}, doPendingOperator},   // dd or dy (delete / cut)
	{ncurses::Key{'y'}, doPendingOperator},     // yy or yd (yank / cut)
	{ncurses::Key{'p'}, putLines},
	{ncurses::Key{'P'}, putLines},
	{ncurses::Key{'i'}, startInsert},
	{ncurses::Key{'a'}, startInsert},
	{ncurses::Key{'o'}, startInsert},
	// TODO not implemented: Command mode
	// {ncurses::Key{'/'}, startCommand},
	{ncurses::Key{':'}, startCommand},
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
	{ncurses::Key::Backspace, deleteChars},
	{ncurses::Key::Enter, breakLine},
};

auto commandOps = std::unordered_map<ncurses::Key, OperatorFunction>{
	// {ncurses::Key::Right, ...},
	// {ncurses::Key::Left, ...},
	// {ncurses::Key::Down, ...},
	// {ncurses::Key::Up, ...},
	// {ncurses::Key::End, ...},
	// {ncurses::Key::Home, ...},
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

bool commandMatches(
	std::string_view const command,
	std::string_view const requiredPrefix,
	std::string_view const fullCommand
)
{
	return command.starts_with(requiredPrefix) && fullCommand.starts_with(command);
}

void Editor::handleKey(ncurses::Key k)
{
	switch (mode)
	{
		case Mode::Normal:
			if (normalOps.contains(k))
			{
				auto res = normalOps[k]({
					.key=k, .buffer=buffer, .reg=reg,
					.cursor=cursor, .windowInfo=windowInfo, .currentMode=mode,
					.pendingOperator=pendingOperator,
					.count=operatorCount
				});

				pendingOperator = res.pendingOperator;

			if (operatorCount.has_value() && not res.count.has_value())  // clear count indication
				{
					statusLine.erase();
					statusLine.refresh();
					editorWindow.refresh();  // return cursor to editor
				}

				operatorCount = res.count;

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

				if (res.message != "")
				{
					statusLine.erase();
					statusLine.mvaddstr({}, res.message);
					statusLine.refresh();
					editorWindow.refresh();  // return cursor to editor
				}
				else if (operatorCount.has_value())
				{
					statusLine.erase();
					statusLine.mvaddstr({statusLine.get_rect().s.w - 10, 0}, std::to_string(*operatorCount));
					statusLine.refresh();
					editorWindow.refresh();  // return cursor to editor
				}
			}
			break;

		case Mode::Insert:
			if (insertOps.contains(k))
			{
				auto res = insertOps[k]({k, buffer, reg, cursor, windowInfo, mode});
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

		case Mode::Command:
			if (commandOps.contains(k))
			{
				auto res = commandOps[k]({k, buffer, reg, cursor, windowInfo, mode});
				auto needToRepaint = false;
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
			else if (k == ncurses::Key::Enter)
			{
				statusLine.erase();
				if (commandMatches(cmdline, "f", "file"))
				{
					auto percentage = (cursor.line + 1) * 100 / buffer.numLines();
					statusLine.mvaddstr(
						{},
						"\"filename\" " + std::to_string(buffer.numLines()) + " lines " +
							"--" + std::to_string(percentage) + "%--"
					);
				}
				else if (commandMatches(cmdline, "q", "quit"))
				{
					quit = true;
				}
				statusLine.refresh();
				// TODO :e[dit], :w[rite], :r[ead]
				cmdline = "";
				cmdlineCursor = 0;
				mode = Mode::Normal;
			}
			else
			{
				auto ch = static_cast<int>(k);
				if (ch < 256 && std::isprint(ch))
				{
					cmdline += ch;
					cmdlineCursor++;
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
			break;

		case Mode::Insert:
			statusLine.mvaddstr({0,0}, "-- INSERT --");
			break;

		case Mode::Command:
			statusLine.mvaddstr({0,0}, ":" + cmdline);
			break;
	}
	statusLine.refresh();

	editorWindow.refresh();

	switch (mode)
	{
		case Mode::Normal:
		case Mode::Insert:
			editorWindow.move(getScreenCursorPosition());
			break;

		case Mode::Command:
			statusLine.move({1 + cmdlineCursor, 0});
			break;
	}
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
	while (not quit)
	{
		ncurses::Key ch;
		switch(mode)
		{
			case Mode::Normal:
			case Mode::Insert:
				ch = editorWindow.getch();
				break;

			case Mode::Command:
				ch = statusLine.getch();
				break;
		}
		if (ch == ncurses::Key::Ctrl({'c'}))  // Ctrl+C
		{
			break;
		}
		handleKey(ch);
	}
	return 0;
}
