#include "editor.h"

#include <cctype>
#include <fstream>
#include <numeric>
#include <string>

#include "ncursespp/color.h"

void Editor::Buffer::erase(int line, int col, int count)
{
	lines[line].erase(col, count);
}

void Editor::Buffer::insert(int line, int col, char ch)
{
	lines[line].insert(col, 1, ch);
}

void Editor::Buffer::breakLine(int line, int col)
{
	std::size_t colIndex = col;
	if (lines[line].length() == colIndex)
	{
		lines.emplace(lines.begin() + line + 1);
	}
	else
	{
		auto tail = lines[line].substr(colIndex);
		lines[line] = lines[line].substr(0, colIndex);
		lines.emplace(lines.begin() + line + 1, tail);
	}
}

void Editor::Buffer::joinLines(int line, int n)
{
	if (n <= 1)  // nothing to be done
	{
		return;
	}
	lines[line] = std::accumulate(
		lines.cbegin() + line,
		lines.cbegin() + line + n,
		std::string{}, [](auto const& acc, auto const& lhs) { return acc + lhs; }
	);
	lines.erase(lines.cbegin() + line + 1, lines.cbegin() + line + n);
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
	editorWindow.setbackground(ncurses::Color::Blue);
	editorWindow.setcolor(ncurses::Color::Yellow, ncurses::Color::Blue);
	lineNumbers.setbackground(ncurses::Color::Purple);
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
			switch (k)
			{
				case 'i':
					mode = Mode::Insert;
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

				case ncurses::Key::Left:
					cursorCol = std::max(0, cursorCol - 1);
					break;

				case ncurses::Key::Right:
					if (buffer.is_empty())
						break;
					cursorCol = std::min(cursorCol+1, static_cast<int>(buffer.getLine(cursorLine).length()));
					break;

				case ncurses::Key::Up:
					if (cursorLine > 0)
					{
						cursorLine--;
					}
					break;

				case ncurses::Key::Down:
					if (cursorLine < buffer.numLines() - 1)
					{
						cursorLine++;
					}
					break;

				case ncurses::Key::Backspace:
					if (cursorCol > 0)
					{
						buffer.erase(cursorLine, cursorCol-1, 1);
						cursorCol--;
					}
					else if (cursorLine > 0)
					{
						cursorCol = buffer.getLine(cursorLine-1).length();
						cursorLine--;
						buffer.joinLines(cursorLine, 2);
					}
					break;

				case ncurses::Key::Enter:
					buffer.breakLine(cursorLine, cursorCol);
					cursorCol = 0;
					cursorLine++;
					break;

				case ncurses::Key::Home:
					cursorCol = 0;
					break;

				case ncurses::Key::End:
					cursorCol = static_cast<int>(buffer.getLine(cursorLine).length());
					break;

				case ncurses::Key::PageUp:
					topLine = std::max(0, topLine - editorWindow.get_rect().s.h);
					if (cursorLine >= topLine + editorWindow.get_rect().s.h)
					{
						cursorLine = topLine + editorWindow.get_rect().s.h - 1;
					}
					break;

				case ncurses::Key::PageDown:
					topLine = std::min(topLine + editorWindow.get_rect().s.h, buffer.numLines() - 1);
					if (cursorLine < topLine)
					{
						cursorLine = topLine;
					}
					break;

				default:
					auto ch = static_cast<int>(k);
					if (ch < 256 && std::isprint(ch))
					{
						buffer.insert(cursorLine, cursorCol, static_cast<char>(ch));
						cursorCol++;
					}
					break;
			}
			break;
	}

	cursorCol = std::min(cursorCol, static_cast<int>(buffer.getLine(cursorLine).length()));
	if (topLine > cursorLine)
	{
		topLine = cursorLine;
	}
	while (getScreenCursorPosition().y >= editorWindow.get_rect().s.h)
	{
		topLine++;
	}
	if (not wrap)
	{
		while (cursorCol - leftCol >= editorWindow.get_rect().s.w)
		{
			leftCol += 20;
		}
		while (cursorCol - leftCol < 0)
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
	for (auto i = topLine; i < cursorLine; i++)
	{
		pos.y += getLineVirtualHeight(buffer.getLine(i));
	}

	if (wrap)
	{
		pos.x = cursorCol % editorWindow.get_rect().s.w;
		pos.y += cursorCol / editorWindow.get_rect().s.w;
	}
	else
	{
		pos.x = cursorCol - leftCol;
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

		statusLine.erase();
		statusLine.mvaddstr({0,0}, "<" + std::to_string(ch) + ">");
		statusLine.refresh();
	}
	return 0;
}
