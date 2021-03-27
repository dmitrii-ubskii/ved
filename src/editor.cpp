#include "editor.h"

#include <cctype>
#include <numeric>

#include <string>

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

std::string const& Editor::Buffer::getLine(int idx) const
{
	return lines[idx];
}

// *** //

Editor::Editor()
	: context{}
	, editorWindow{{{0, 0}, {0, context.get_rect().s.h - 1}}}
	, statusLine{{{0, context.get_rect().s.h - 1}, {}}}
{
	context.raw(true);
	context.refresh();
	repaint();
}

void Editor::handleKey(ncurses::Key k)
{
	auto line = cursorPosition.y + topLine;

	switch (k)
	{
		case ncurses::Key::Left:
			cursorPosition.x = std::max(1, cursorPosition.x) - 1;
			break;

		case ncurses::Key::Right:
			if (buffer.is_empty())
				break;
			cursorPosition.x = std::min(cursorPosition.x+1, static_cast<int>(buffer.getLine(line).length()));
			break;

		case ncurses::Key::Up:
			if (cursorPosition.y > 0)
			{
				cursorPosition.y--;
				cursorPosition.x = std::min(cursorPosition.x, static_cast<int>(buffer.getLine(line-1).length()));
			}
			break;

		case ncurses::Key::Down:
			if (cursorPosition.y < buffer.numLines() - 1)
			{
				cursorPosition.y++;
				cursorPosition.x = std::min(cursorPosition.x, static_cast<int>(buffer.getLine(line+1).length()));
			}
			break;

		case ncurses::Key::Backspace:
			if (cursorPosition.x > 0)
			{
				buffer.erase(line, cursorPosition.x-1, 1);
				cursorPosition.x--;
			}
			break;

		case ncurses::Key::Enter:
			buffer.breakLine(line, cursorPosition.x);
			cursorPosition.x = 0;
			cursorPosition.y++;
			break;

		default:
			auto ch = static_cast<int>(k);
			if (ch < 256 && std::isprint(ch))
			{
				buffer.insert(line, cursorPosition.x, static_cast<char>(ch));
				cursorPosition.x++;
			}
			break;
	}

	repaint();
}

void Editor::repaint()
{
	editorWindow.clear();
	if (not wrap)
	{
		for (auto i = 0; i < editorWindow.get_rect().s.h; i++)
		{
			if (i < buffer.numLines())
			{
				editorWindow.mvaddnstr({0, i}, buffer.getLine(i + topLine), editorWindow.get_rect().s.w);
			}
			else
			{
				editorWindow.mvaddstr({0, i}, "~");
			}
		}
	}

	editorWindow.refresh();
	editorWindow.move(cursorPosition);

	context.refresh();
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

		statusLine.clear();
		statusLine.mvaddstr({0,0}, "<" + std::to_string(ch) + ">");
		statusLine.refresh();
	}
	return 0;
}
