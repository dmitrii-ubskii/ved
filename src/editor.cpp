#include "editor.h"

#include <cassert>
#include <cctype>
#include <fstream>
#include <numeric>
#include <string>

#include <wordexp.h>

#include "ncursespp/color.h"

#include "ops.h"

void Editor::Buffer::erase(CursorPosition p, int count)
{
	assert(p.line >= 0);
	auto lineIndex = static_cast<std::size_t>(p.line);
	assert(p.col >= 0);
	auto colIndex = static_cast<std::size_t>(p.col);
	assert(count > 0);
	auto sizeCount = static_cast<std::size_t>(count);

	lines[lineIndex].erase(colIndex, sizeCount);
}

void Editor::Buffer::insert(CursorPosition p, char ch, int count)
{
	if (isEmpty())
	{
		assert(p.line == 0 && p.col == 0);
		lines.push_back("");
	}

	assert(p.line >= 0);
	auto lineIndex = static_cast<std::size_t>(p.line);
	assert(p.col >= 0);
	auto colIndex = static_cast<std::size_t>(p.col);
	assert(count > 0);
	auto sizeCount = static_cast<std::size_t>(count);

	lines[lineIndex].insert(colIndex, sizeCount, ch);
}

void Editor::Buffer::insertLine(int line)
{
	if (isEmpty())
	{
		assert(line == 0);
		lines.push_back("");
	}

	lines.insert(lines.begin() + line + 1, "");
}

void Editor::Buffer::breakLine(CursorPosition p)
{
	if (isEmpty())
	{
		assert(p.line == 0 && p.col == 0);
		lines.push_back("");
	}

	assert(p.line >= 0);
	auto lineIndex = static_cast<std::size_t>(p.line);
	assert(p.col >= 0);
	auto colIndex = static_cast<std::size_t>(p.col);
	if (lines[lineIndex].length() == colIndex)
	{
		lines.emplace(lines.begin() + p.line + 1);
	}
	else
	{
		auto tail = lines[lineIndex].substr(colIndex);
		lines[lineIndex] = lines[lineIndex].substr(0, colIndex);
		lines.emplace(lines.begin() + p.line + 1, tail);
	}
}

void Editor::Buffer::joinLines(int line, int count)
{
	if (count <= 1)  // nothing to be done
	{
		return;
	}

	assert(line >= 0);
	auto lineIndex = static_cast<std::size_t>(line);

	lines[lineIndex] = std::accumulate(
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
	if (isEmpty())
	{
		assert(line == 0);
		lines.push_back("");
	}
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
}

int Editor::Buffer::numLines() const
{
	return static_cast<int>(lines.size());
}

bool Editor::Buffer::isEmpty() const
{
	return numLines() == 0;
}

void Editor::Buffer::clear()
{
	lines.clear();
}

void Editor::Buffer::read(std::filesystem::path const& filePath)
{
	auto fileHandler = std::ifstream(filePath);
	for (std::string lineBuffer; std::getline(fileHandler, lineBuffer); )
	{
		lines.push_back(lineBuffer);
	}
	fileHandler.close();
}

void Editor::Buffer::read(std::filesystem::path const& filePath, int line)
{
	auto fileHandler = std::ifstream(filePath);
	auto it = lines.begin() + line + 1;
	for (std::string lineBuffer; std::getline(fileHandler, lineBuffer); )
	{
		it = lines.insert(it, lineBuffer) + 1;
	}
	fileHandler.close();
}

void Editor::Buffer::write(std::filesystem::path const& filePath) const
{
	auto fileHandler = std::ofstream(filePath);
	for (auto& line: lines)
	{
		fileHandler << line << "\n";
	}
	fileHandler.close();
}

int Editor::Buffer::lineLength(int idx) const
{
	if (isEmpty())
	{
		return 0;
	}
	assert(idx >= 0);
	auto index = static_cast<std::size_t>(idx);
	return static_cast<int>(lines[index].length());
}

std::string const& Editor::Buffer::getLine(int idx) const
{
	assert(idx >= 0);
	auto index = static_cast<std::size_t>(idx);
	return lines[index];
}

// *** //

Editor::Editor()
	: context{}
	, editorWindow{{{4, 0}, {0, context.get_rect().s.h - 1}}}
	, lineNumbers({{0, 0}, {4, context.get_rect().s.h - 1}})
	, statusLine{{{0, context.get_rect().s.h - 1}, {}}}
{
	context.raw(true);
	editorWindow.setbackground(ncurses::Color::White, ncurses::Color::Black);
	editorWindow.setcolor(ncurses::Color::White, ncurses::Color::Black);
	lineNumbers.setbackground(ncurses::Color::Gray, ncurses::Color::Black);
	lineNumbers.setcolor(ncurses::Color::Gray, ncurses::Color::Black);
	context.refresh();
	repaint();
}

std::filesystem::path resolvePath(std::filesystem::path const& path)
{
	wordexp_t p;
	wordexp(path.c_str(), &p, 0);
	auto resolvedPath = std::filesystem::path{p.we_wordv[p.we_offs]};
	wordfree(&p);
	return resolvedPath;
}

void Editor::open(std::filesystem::path const& path, Force force)
{
	auto resolvedPath = resolvePath(path);
	if (not std::filesystem::exists(resolvedPath))
	{
		displayMessage("ERR: Could not open `" + path.string() + "': file does not exist");
		return;
	}
	if (not std::filesystem::is_regular_file(resolvedPath))
	{
		displayMessage("ERR: Could not open `" + path.string() + "': not a regular file");
		return;
	}
	if (modified and force == Force::No)
	{
		displayMessage("ERR: No write since last change (add ! to override)");
		return;
	}

	file = resolvedPath;

	buffer.clear();
	buffer.read(file);
	modified = false;

	cursor.line = std::min(cursor.line, buffer.numLines()-1);
	auto cursorLineLength = buffer.lineLength(cursor.line);
	cursor.col = std::min(cursor.col, std::max(0, cursorLineLength - 1));
	
	adjustViewport();
	repaint();
}

void Editor::read(std::filesystem::path const& path)
{
	auto resolvedPath = resolvePath(path);
	if (not std::filesystem::exists(resolvedPath))
	{
		displayMessage("ERR: Could not open `" + path.string() + "': file does not exist");
		return;
	}
	if (not std::filesystem::is_regular_file(resolvedPath))
	{
		displayMessage("ERR: Could not open `" + path.string() + "': not a regular file");
		return;
	}

	auto prevLines = buffer.numLines();
	buffer.read(resolvedPath, cursor.line);
	auto newLines = buffer.numLines() - prevLines;

	modified = true;

	repaint();

	displayMessage("\"" + resolvedPath.string() + "\" " + std::to_string(newLines) + " lines read");
}

void Editor::write(std::filesystem::path const& path, Force force)
{
	auto resolvedPath = resolvePath(path);
	if (std::filesystem::exists(resolvedPath) && file != resolvedPath)
	{
		if (force == Force::Yes)
		{
			if (not std::filesystem::is_regular_file(resolvedPath))
			{
				displayMessage("ERR: Could not open `" + path.string() + "' for writing: not a regular file");
				return;
			}
		}
		else
		{
			displayMessage("ERR: File exists (add ! to override)");
			return;
		}
	}

	buffer.write(resolvedPath);
	displayMessage("\"" + resolvedPath.string() + "\" " + std::to_string(buffer.numLines()) + " lines written");
	modified = false;
}

bool commandMatches(
	std::string_view const command,
	std::string_view const requiredPrefix,
	std::string_view const fullCommand
)
{
	return command.starts_with(requiredPrefix) && fullCommand.starts_with(command);
}

std::vector<std::string> Editor::parseCommand()
{
	if (cmdline == ":")
	{
		return {};
	}

	auto parsedCommand = std::vector<std::string>{};

	auto firstSpace = cmdline.find(" ");
	auto command = cmdline.substr(1, firstSpace - 1);

	if (command.ends_with("!"))
	{
		parsedCommand.emplace_back(command.substr(0, command.length() - 1));
		parsedCommand.push_back("!");
	}
	else
	{
		parsedCommand.emplace_back(command);
	}

	if (firstSpace != std::string::npos)
	{
		auto tailStart = cmdline.find_first_not_of(" ", firstSpace);
		if (tailStart != std::string::npos)
		{
			auto tail = cmdline.substr(tailStart, cmdline.find_last_not_of(" "));
			if (tail.find_first_of(" ") != std::string::npos)
			{
				displayMessage("ERR: Trailing characters");
				return {};
			}
			parsedCommand.emplace_back(tail);
		}
	}

	return parsedCommand;
}

void Editor::executeCommand()
{
	auto parsedCommand = parseCommand();
	if (parsedCommand.empty())
	{
		return;
	}

	statusLine.erase();

	auto const& command = parsedCommand[0];
	auto numTokens = parsedCommand.size();
	auto force = Force::No;
	if (numTokens > 1 && parsedCommand[1] == "!")
	{
		force = Force::Yes;
	}
	auto arg = std::optional<std::string>{};
	if (numTokens > 1 && parsedCommand[numTokens-1] != "!")
	{
		arg = parsedCommand[numTokens-1];
	}

	if (commandMatches(command, "f", "file"))
	{
		if (force == Force::Yes || arg.has_value())
		{
			displayMessage("ERR: Trailing characters");
		}
		else
		{
			auto fileName = file.string();
			if (fileName == "")
			{
				fileName = "[No Name]";
			}
			auto stats = std::string{"--No lines in buffer--"};
			if (buffer.numLines())
			{
				auto percentage = std::to_string((cursor.line + 1) * 100 / buffer.numLines()) + "%";
				stats = std::to_string(buffer.numLines()) + " lines " + "--" + percentage + "--";
			}
			if (modified)
			{
				stats = "[Modified] " + stats;
			}
			displayMessage(
				"\"" + fileName + "\" " + stats
			);
		}
	}
	else if (commandMatches(command, "q", "quit"))
	{
		if (arg.has_value())
		{
			displayMessage("ERR: Trailing characters");
		}
		else if (modified and force == Force::No)
		{
			displayMessage("ERR: No write since last change (add ! to override)");
		}
		else
		{
			quit = true;
		}
	}
	else if (commandMatches(command, "e", "edit"))
	{
		if (arg.has_value())
		{
			open(*arg, force);
		}
		else if (file != "")
		{
			open(file, force);
		}
		else
		{
			displayMessage("ERR: No file name");
		}
	}
	else if (commandMatches(command, "w", "write"))
	{
		if (arg.has_value())
		{
			write(*arg);
		}
		else if (file != "")
		{
			write(file, force);
		}
		else
		{
			displayMessage("ERR: No file name");
		}
	}
	else if (commandMatches(command, "r", "read"))
	{
		if (force == Force::Yes)
		{
			displayMessage("ERR: No ! allowed");
		}
		else if (arg.has_value())
		{
			read(*arg);
		}
		else
		{
			displayMessage("ERR: No file name");
		}
	}
	else
	{
		displayMessage("ERR: Not an editor command: " + command);
	}
}

void Editor::doSearch()
{
	auto searchString = cmdline.substr(1);

	for (auto line = cursor.line; line < buffer.numLines(); line++)
	{
		auto startPos = line == cursor.line ? static_cast<std::size_t>(cursor.col + 1) : 0;
		if (
			auto pos = buffer.getLine(line).find(searchString, startPos);
			pos != std::string::npos
		)
		{
			cursor.line = line;
			cursor.col = static_cast<int>(pos);
			adjustViewport();
			repaint();
			return;
		}
	}

	for (auto line = 0; line <= cursor.line; line++)
	{
		if (auto pos = buffer.getLine(line).find(searchString); pos != std::string::npos)
		{
			cursor.line = line;
			cursor.col = static_cast<int>(pos);
			adjustViewport();
			repaint();
			displayMessage("search hit BOTTOM, continuing at TOP");
			return;
		}
	}

	displayMessage("ERR: Search string not found: " + searchString);
}

void Editor::handleKey(ncurses::Key k)
{
	switch (mode)
	{
		case Mode::Normal:
			if (normalOps.contains(k))
			{
				auto res = normalOps[k]({
					.key=k, .context=context, .buffer=buffer, .reg=reg,
					.cursor=cursor, .windowInfo=windowInfo, .currentMode=mode,
					.pendingOperator=pendingOperator,
					.count=operatorCount
				});

				pendingOperator = res.pendingOperator;

				if (operatorCount.has_value() && not res.count.has_value())  // clear count indication
				{
					displayMessage("");
				}

				operatorCount = res.count;

				if (res.bufferChanged)
				{
					modified = true;
				}
				auto needToRepaint = res.bufferChanged;
				if (res.cursorMoved)
				{
					cursor = res.cursorPosition;
					adjustViewport();
					needToRepaint = true;
					editorWindow.move(getScreenCursorPosition());
				}
				if (res.modeChanged)
				{
					mode = res.newMode;
					needToRepaint = true;
					if (mode == Mode::Command)
					{
						switch (k)
						{
							case ':': case ';':
								cmdline = ":";
								break;

							case '/':
								cmdline = "/";
								break;

							default:
								throw;
						}
						cmdlineCursor = 1;
					}
				}
				if (needToRepaint)
				{
					repaint();
				}

				if (res.message != "")
				{
					displayMessage(res.message);
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
				auto res = insertOps[k]({k, context, buffer, reg, cursor, windowInfo, mode});
				if (res.bufferChanged)
				{
					modified = true;
				}
				auto needToRepaint = res.bufferChanged;
				if (res.cursorMoved)
				{
					cursor = res.cursorPosition;
					adjustViewport();
					needToRepaint = true;
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
				auto ch = k.keycode;
				if (ch < 256 && std::isprint(ch))
				{
					buffer.insert(cursor, static_cast<char>(ch), 1);
					cursor.col++;
					modified = true;
				}
				adjustViewport();
				repaint();
			}
			break;

		case Mode::Command:
			if (commandOps.contains(k))
			{
				auto res = commandOps[k]({k, cmdline, cmdlineCursor});

				auto needToRepaint = res.cmdlineChanged;
				if (res.cursorMoved)
				{
					cmdlineCursor = res.cursorPosition;
				}
				if (res.modeChanged)
				{
					mode = res.newMode;
					repaint();

					if (cmdline.starts_with(':'))
					{
						executeCommand();
					}
					else if (cmdline.starts_with('/'))
					{
						doSearch();
					}

					cmdline = "";
					cmdlineCursor = 0;
					needToRepaint = false;
				}
				if (needToRepaint)
				{
					repaint();
				}

				if (res.message != "")
				{
					displayMessage(res.message);
				}
			}
			else
			{
				auto ch = k.keycode;
				if (ch < 256 && std::isprint(ch))
				{
					cmdline += static_cast<char>(ch);
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
	
	assert(windowInfo.leftCol >= 0);
	auto leftEdge = static_cast<std::size_t>(windowInfo.leftCol);

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
				editorWindow.mvaddnstr(
					{0, lineY},
					buffer.getLine(i).substr(leftEdge),
					editorWindow.get_rect().s.w
				);
			}
		}
		auto lineNumber = std::to_string(i + 1);
		auto x = 3 - static_cast<int>(lineNumber.length());
		lineNumbers.mvaddnstr({x, lineY}, lineNumber, lineNumbers.get_rect().s.w-1);
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
			statusLine.mvaddstr({0,0}, cmdline);
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
			statusLine.move({cmdlineCursor, 0});
			break;
	}
}

void Editor::displayMessage(std::string_view message)
{
	statusLine.clear();
	statusLine.mvaddstr({}, message);
	statusLine.refresh();
	editorWindow.refresh();
}

std::size_t Editor::getLineLength(std::string_view lineContents) const
{
	return lineContents.length();
}

int Editor::getLineVirtualHeight(std::string_view lineContents) const
{
	if (not wrap)
	{
		return 1;
	}
	auto width = editorWindow.get_rect().s.w;
	assert(width > 0);
	return static_cast<int>(lineContents.length() + 1) / width + 1;
}

void Editor::adjustViewport()
{
	if (windowInfo.topLine > cursor.line)
	{
		windowInfo.topLine = cursor.line;
	}
	while (getScreenCursorPosition().y >= editorWindow.get_rect().s.h)
	{
		windowInfo.topLine++;
	}
	if (not wrap)
	{
		while (cursor.col - windowInfo.leftCol >= editorWindow.get_rect().s.w)
		{
			windowInfo.leftCol += 20;
		}
		while (windowInfo.leftCol > cursor.col)
		{
			windowInfo.leftCol -= 20;
		}
	}
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
