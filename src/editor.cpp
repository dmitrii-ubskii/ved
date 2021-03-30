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

std::string const& Editor::Buffer::getLine(int idx) const
{
	return lines[idx];
}

// *** //

Editor::Editor()
	: context{}
	, editorWindow{{{3, 0}, {0, context.get_rect().s.h - 1}}}
	, lineNumbers({{0, 0}, {3, context.get_rect().s.h - 1}})
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
	switch (k)
	{
		case ncurses::Key::Left:
			cursor.col = std::max(0, cursor.col - 1);
			break;

		case ncurses::Key::Right:
			if (buffer.is_empty())
				break;
			cursor.col = std::min(cursor.col+1, static_cast<int>(buffer.getLine(cursor.line).length()));
			break;

		case ncurses::Key::Up:
			if (cursor.line > 0)
			{
				cursor.line--;
			}
			break;

		case ncurses::Key::Down:
			if (cursor.line < buffer.numLines() - 1)
			{
				cursor.line++;
			}
			break;

		case ncurses::Key::Home:
			cursor.col = 0;
			break;

		case ncurses::Key::End:
			cursor.col = static_cast<int>(buffer.getLine(cursor.line).length());
			break;

		case ncurses::Key::PageUp:
			topLine = std::max(0, topLine - editorWindow.get_rect().s.h);
			if (cursor.line >= topLine + editorWindow.get_rect().s.h)
			{
				cursor.line = topLine + editorWindow.get_rect().s.h - 1;
			}
			break;

		case ncurses::Key::PageDown:
			topLine = std::min(topLine + editorWindow.get_rect().s.h, buffer.numLines() - 1);
			if (cursor.line < topLine)
			{
				cursor.line = topLine;
			}
			break;
	}

	switch (mode)
	{
		case Mode::Normal:
			switch (k)
			{
				case 'i':
					mode = Mode::Insert;
					break;

				case '0':
					cursor.col = 0;
					break;

				case '$':
					cursor.col = static_cast<int>(buffer.getLine(cursor.line).length());
					break;

				case ' ':
					cursor.col++;
					if (cursor.col >= static_cast<int>(buffer.getLine(cursor.line).length()) && cursor.line < buffer.numLines() - 1)
					{
						cursor.line++;
						cursor.col = 0;
					}
					break;

				case ncurses::Key::Backspace:
					if (cursor.col > 0)
					{
						cursor.col--;
					}
					else if (cursor.line > 0)
					{
						cursor.col = buffer.getLine(cursor.line-1).length();
						cursor.line--;
					}
					break;

				case '-':
					if (cursor.line > 0)
					{
						cursor.line--;
						cursor.col = 0;
					}
					break;

				case ncurses::Key::Enter:
					if (cursor.line < buffer.numLines() - 1)
					{
						cursor.line++;
						cursor.col = 0;
					}
					break;

				case 'h':
					cursor.line = topLine;
					break;

				case 'l':
					cursor.line = std::min(topLine + editorWindow.get_rect().s.h - 1, buffer.numLines() - 1);
					break;

				case 'b':
					cursor.line = 0;
					topLine = 0;
					break;

				default:
					break;
			}
			break;

		case Mode::Insert:
			switch (k)
			{
				case ncurses::Key::Escape:
					mode = Mode::Normal;
					break;

				case ncurses::Key::Backspace:
					if (cursor.col > 0)
					{
						cursor.col--;
						buffer.erase(cursor, 1);
					}
					else if (cursor.line > 0)
					{
						cursor.col = buffer.getLine(cursor.line-1).length();
						cursor.line--;
						buffer.joinLines(cursor.line, 2);
					}
					break;

				case ncurses::Key::Enter:
					buffer.breakLine(cursor);
					cursor.col = 0;
					cursor.line++;
					break;

				default:
					auto ch = static_cast<int>(k);
					if (ch < 256 && std::isprint(ch))
					{
						buffer.insert(cursor, static_cast<char>(ch));
						cursor.col++;
					}
					break;
			}
			break;
	}

	if (mode == Mode::Insert)
	{
		cursor.col = std::min(cursor.col, static_cast<int>(buffer.getLine(cursor.line).length()));
	}
	else if (mode == Mode::Normal)
	{
		cursor.col = std::min(cursor.col, std::max(0, static_cast<int>(buffer.getLine(cursor.line).length()) - 1));
	}
	if (topLine > cursor.line)
	{
		topLine = cursor.line;
	}
	while (getScreenCursorPosition().y >= editorWindow.get_rect().s.h)
	{
		topLine++;
	}
	if (not wrap)
	{
		while (cursor.col - leftCol >= editorWindow.get_rect().s.w)
		{
			leftCol += 20;
		}
		while (cursor.col - leftCol < 0)
		{
			leftCol -= 20;
		}
	}

	repaint();
}

void Editor::repaint()
{
	editorWindow.erase();
	lineNumbers.erase();
	auto lineY = 0;
	for (auto i = topLine; i < buffer.numLines(); i++)
	{
		if (wrap)
		{
			editorWindow.mvaddstr({0, lineY}, buffer.getLine(i));
		}
		else
		{
			if (buffer.getLine(i).length() > static_cast<std::size_t>(leftCol))
			{
				editorWindow.mvaddnstr({0, lineY}, buffer.getLine(i).substr(leftCol), editorWindow.get_rect().s.w);
			}
		}
		lineNumbers.mvaddnstr({0, lineY}, std::to_string(i + 1), lineNumbers.get_rect().s.w);
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
	for (auto i = topLine; i < cursor.line; i++)
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
		pos.x = cursor.col - leftCol;
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
