#include "editor.h"

#include <cctype>
#include <fstream>
#include <numeric>
#include <string>

#include "ncursespp/color.h"

#include "ops.h"

void Editor::Buffer::erase(CursorPosition p, int count)
{
	lines[p.line].erase(p.col, count);
}

void Editor::Buffer::insert(CursorPosition p, char ch, int count)
{
	lines[p.line].insert(p.col, count, ch);
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
	file = path;
	buffer.read(file);
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
					.key=k, .context=context, .buffer=buffer, .reg=reg,
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
				auto res = insertOps[k]({k, context, buffer, reg, cursor, windowInfo, mode});
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
					buffer.insert(cursor, static_cast<char>(ch), 1);
					cursor.col++;
				}
				repaint();
			}
			break;

		case Mode::Command:
			if (commandOps.contains(k))
			{
				auto res = commandOps[k]({k, context, buffer, reg, cursor, windowInfo, mode});
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
						"\"" + file.string() + "\" " + std::to_string(buffer.numLines()) + " lines " +
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
